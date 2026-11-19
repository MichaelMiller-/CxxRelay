#pragma once

#include <cxxrelay/tool_dispatcher.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace CxxRelay
{
   // Transport-neutral function calls. A call_id belongs to exactly one model response.
   struct LlmToolCall
   {
      std::string call_id;
      std::string name;
      std::string arguments_json;
   };

   struct LlmInputItem
   {
      enum class Kind { UserMessage, ToolOutput } kind{};
      std::string content; // user text or serialized ToolResult
      std::string call_id; // nonempty for ToolOutput

      static LlmInputItem user(std::string text) { return {Kind::UserMessage, std::move(text), {}}; }
      static LlmInputItem tool_result(std::string call_id, std::string output)
      {
         return {Kind::ToolOutput, std::move(output), std::move(call_id)};
      }
   };

   struct LlmRequest
   {
      std::vector<LlmInputItem> input;
      std::optional<std::string> previous_response_id;
      // Output of ToolDispatcher::definitions(context); filtered by the trusted host.
      Value tools = Value::array();
   };

   struct LlmResponse
   {
      std::string response_id;
      std::string text;
      std::vector<LlmToolCall> tool_calls;
   };

   using TextDeltaCallback = std::function<void(std::string_view)>;

   // Error messages are for application diagnostics; never show raw network errors
   // or API credentials directly to the user or model.
   class LlmError : public std::runtime_error
   {
   public:
      using std::runtime_error::runtime_error;
   };
   class ChatCancelled final : public LlmError
   {
   public:
      ChatCancelled() : LlmError("Chat operation cancelled") {}
   };

   class ILLMProvider
   {
   public:
      virtual ~ILLMProvider() = default;
      virtual LlmResponse complete(const LlmRequest& request, std::stop_token stop,
                                   TextDeltaCallback on_text_delta) = 0;
   };

   struct ChatEntry
   {
      enum class Role { User, Assistant, Tool } role{};
      std::string text;
      std::string tool_name;
   };

   struct ChatApprovalRequest
   {
      std::string approval_id;
      std::string call_id;
      std::string tool_name;
      Value arguments;
   };

   struct ChatTurnResult
   {
      enum class Status { Completed, AwaitingConfirmation } status{Status::Completed};
      std::string answer;
      std::optional<ChatApprovalRequest> approval;
   };

   struct ChatLimits
   {
      std::size_t max_model_responses{8};
      std::size_t max_tool_calls{24};
      std::size_t max_user_message_bytes{32 * 1024};
   };

   // Synchronous, single-owner state machine. Execute send()/resume() on a worker
   // thread, and deliver callbacks to the GUI thread via its event mechanism.
   // Cancellation uses the caller's std::stop_token (e.g. std::jthread).
   // A cancelled or failed turn makes the session unusable until reset(), since
   // the remote provider may have accepted a response even if we did not receive it.
   class ChatOrchestrator final
   {
   public:
      ChatOrchestrator(ILLMProvider& provider, const ToolDispatcher& tools, ChatLimits limits = {});

      [[nodiscard]] ChatTurnResult send(std::string text, const ToolContext& context, std::stop_token stop = {},
                                        TextDeltaCallback on_delta = {});
      // Must only be called from trusted host GUI code after the user has reviewed
      // the exact approval request. approval_id binds the response and tool call.
      [[nodiscard]] ChatTurnResult resume(std::string_view approval_id, bool approved, const ToolContext& context,
                                          std::stop_token stop = {}, TextDeltaCallback on_delta = {});

      void reset();
      [[nodiscard]] bool awaiting_confirmation() const noexcept { return approval_.has_value(); }
      [[nodiscard]] bool failed() const noexcept { return failed_; }
      [[nodiscard]] const std::vector<ChatEntry>& history() const noexcept { return history_; }
      [[nodiscard]] const std::optional<std::string>& response_id() const noexcept { return previous_response_id_; }

   private:
      struct Batch
      {
         std::vector<LlmToolCall> calls;
         std::size_t next{0};
         std::vector<LlmInputItem> outputs;
      };
      [[nodiscard]] ChatTurnResult consume_response(LlmResponse response, const ToolContext& context,
                                                    std::stop_token stop, const TextDeltaCallback& on_delta);
      [[nodiscard]] ChatTurnResult drive(const ToolContext& context, std::stop_token stop,
                                         const TextDeltaCallback& on_delta);
      [[nodiscard]] LlmResponse request_model(std::vector<LlmInputItem> input, const ToolContext& context,
                                              std::stop_token stop, const TextDeltaCallback& on_delta);
      void add_tool_output(const LlmToolCall& call, ToolResult result);
      void throw_if_unusable() const;

      ILLMProvider& provider_;
      const ToolDispatcher& tools_;
      ChatLimits limits_;
      std::vector<ChatEntry> history_;
      std::optional<std::string> previous_response_id_;
      std::optional<Batch> batch_;
      std::optional<ChatApprovalRequest> approval_;
      std::string active_principal_;
      std::size_t response_count_{};
      std::size_t tool_count_{};
      bool failed_{};
   };

} // namespace CxxRelay
