#include <cxxrelay/chat.h>

#include <set>
#include <utility>

namespace CxxRelay
{
   namespace
   {
      void check_stop(std::stop_token stop)
      {
         if (stop.stop_requested())
            throw ChatCancelled{};
      }
      ToolResult rejected_result()
      {
         return {.code = ToolErrorCode::ConfirmationDenied,
                 .result = Value(nullptr),
                 .message = "User did not approve the operation"};
      }
   } // namespace

   ChatOrchestrator::ChatOrchestrator(ILLMProvider& provider, const ToolDispatcher& tools, ChatLimits limits)
       : provider_(provider), tools_(tools), limits_(limits)
   {
      if (!limits.max_model_responses || !limits.max_tool_calls || !limits.max_user_message_bytes)
         throw std::invalid_argument("Chat limits must be positive");
   }

   void ChatOrchestrator::throw_if_unusable() const
   {
      if (failed_)
         throw LlmError("Chat session failed or was cancelled; reset required");
   }

   void ChatOrchestrator::reset()
   {
      history_.clear();
      previous_response_id_.reset();
      batch_.reset();
      approval_.reset();
      active_principal_.clear();
      response_count_ = tool_count_ = 0;
      failed_ = false;
   }

   LlmResponse ChatOrchestrator::request_model(std::vector<LlmInputItem> input, const ToolContext& context,
                                               std::stop_token stop, const TextDeltaCallback& on_delta)
   {
      check_stop(stop);
      if (response_count_ >= limits_.max_model_responses)
         throw LlmError("Model response limit exceeded");
      ++response_count_;
      LlmRequest req{std::move(input), previous_response_id_, tools_.definitions(context)};
      auto response = provider_.complete(req, stop, on_delta);
      check_stop(stop);
      if (response.response_id.empty())
         throw LlmError("Provider returned an empty response ID");
      previous_response_id_ = response.response_id;
      return response;
   }

   ChatTurnResult ChatOrchestrator::send(std::string text, const ToolContext& context, std::stop_token stop,
                                         TextDeltaCallback on_delta)
   {
      throw_if_unusable();
      if (batch_ || approval_)
         throw LlmError("Cannot start a new turn while approval is pending");
      if (text.empty() || text.size() > limits_.max_user_message_bytes)
         throw std::invalid_argument("User message is empty or exceeds its size limit");
      // Authorization is tied to one principal across the lifetime of a session.
      if (context.principal.empty() || (!active_principal_.empty() && context.principal != active_principal_))
         throw LlmError("Chat principal missing or changed; reset required");
      active_principal_ = context.principal;
      response_count_ = tool_count_ = 0;
      history_.push_back({ChatEntry::Role::User, text, {}});
      try {
         return consume_response(request_model({LlmInputItem::user(std::move(text))}, context, stop, on_delta), context,
                                 stop, on_delta);
      } catch (...) {
         failed_ = true;
         throw;
      }
   }

   ChatTurnResult ChatOrchestrator::consume_response(LlmResponse response, const ToolContext& context,
                                                     std::stop_token stop, const TextDeltaCallback& on_delta)
   {
      check_stop(stop);
      if (!response.text.empty())
         history_.push_back({ChatEntry::Role::Assistant, response.text, {}});
      if (response.tool_calls.empty())
         return {ChatTurnResult::Status::Completed, std::move(response.text), std::nullopt};
      // Reject malformed/duplicate identifiers before executing any tool.
      std::set<std::string> identifiers;
      for (const auto& call : response.tool_calls) {
         if (call.call_id.empty() || call.name.empty() || !identifiers.insert(call.call_id).second)
            throw LlmError("Provider returned malformed or duplicate tool-call IDs");
      }
      batch_.emplace(Batch{std::move(response.tool_calls), 0, {}});
      return drive(context, stop, on_delta);
   }

   void ChatOrchestrator::add_tool_output(const LlmToolCall& call, ToolResult result)
   {
      std::string output = result.to_json();
      history_.push_back({ChatEntry::Role::Tool, output, call.name});
      batch_->outputs.push_back(LlmInputItem::tool_result(call.call_id, std::move(output)));
      ++batch_->next;
   }

   ChatTurnResult ChatOrchestrator::drive(const ToolContext& context, std::stop_token stop,
                                          const TextDeltaCallback& on_delta)
   {
      while (batch_) {
         while (batch_->next < batch_->calls.size()) {
            check_stop(stop);
            if (tool_count_ >= limits_.max_tool_calls)
               throw LlmError("Tool-call limit exceeded");
            const auto& call = batch_->calls[batch_->next];
            // Never propagate a confirmation callback from untrusted input or a
            // previously held UI context. Approval happens in resume() only.
            ToolContext guarded = context;
            guarded.confirm_write = {};
            auto result = tools_.dispatch(call.name, call.arguments_json, guarded);
            if (result.code == ToolErrorCode::ConfirmationRequired) {
               Value arguments;
               try {
                  arguments = parse_json(call.arguments_json);
               } catch (const JsonError&) {
                  throw LlmError("Dispatcher accepted invalid JSON");
               }
               approval_ = ChatApprovalRequest{previous_response_id_.value_or("") + ":" + call.call_id, call.call_id,
                                               call.name, std::move(arguments)};
               return {ChatTurnResult::Status::AwaitingConfirmation, {}, approval_};
            }
            ++tool_count_;
            add_tool_output(call, std::move(result));
         }
         auto outputs = std::move(batch_->outputs);
         batch_.reset();
         return consume_response(request_model(std::move(outputs), context, stop, on_delta), context, stop, on_delta);
      }
      throw LlmError("Internal chat state error");
   }

   ChatTurnResult ChatOrchestrator::resume(std::string_view approval_id, bool approved, const ToolContext& context,
                                           std::stop_token stop, TextDeltaCallback on_delta)
   {
      throw_if_unusable();
      if (!approval_ || !batch_ || approval_->approval_id != approval_id)
         throw LlmError("Unknown or stale approval request");
      if (context.principal.empty() || context.principal != active_principal_)
         throw LlmError("Approval principal does not match active session");
      try {
         check_stop(stop);
         const LlmToolCall call = batch_->calls.at(batch_->next);
         ToolResult result;
         if (approved) {
            ToolContext authorized = context;
            // All policy checks are repeated at execution time by ToolDispatcher.
            // Bind approval to both the specific tool and normalized arguments.
            const std::string expected = to_json(approval_->arguments);
            authorized.confirm_write = [name = call.name, expected](const ToolConfirmationRequest& req) {
               return req.tool_name == name && to_json(req.arguments) == expected;
            };
            result = tools_.dispatch(call.name, call.arguments_json, authorized);
         } else {
            result = rejected_result();
         }
         ++tool_count_;
         approval_.reset();
         add_tool_output(call, std::move(result));
         return drive(context, stop, on_delta);
      } catch (...) {
         failed_ = true;
         throw;
      }
   }

} // namespace CxxRelay
