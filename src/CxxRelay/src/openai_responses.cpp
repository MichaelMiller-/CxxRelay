#include <cxxrelay/openai_responses.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace CxxRelay
{
   namespace
   {
      constexpr std::string_view kEndpoint = "https://api.openai.com/v1/responses";

      std::string required_string(const Value& object, std::string_view key)
      {
         return object.at(key).get<std::string>();
      }

      class SseParser
      {
      public:
         SseParser(const TextDeltaCallback& on_delta) : on_delta_(on_delta) {}
         void append(std::string_view chunk)
         {
            if (chunk.size() > kLimit - buffer_.size())
               throw LlmError("Streaming response exceeded its size limit");
            buffer_.append(chunk);
            // Accept both LF and CRLF SSE separators, preserving unconsumed tails.
            for (;;) {
               const auto found = buffer_.find('\n');
               if (found == std::string::npos)
                  break;
               std::string line = buffer_.substr(0, found);
               buffer_.erase(0, found + 1);
               if (!line.empty() && line.back() == '\r')
                  line.pop_back();
               if (line.empty()) {
                  complete_event();
               } else if (line.starts_with("data:")) {
                  std::string_view value{line};
                  value.remove_prefix(5);
                  if (!value.empty() && value.front() == ' ')
                     value.remove_prefix(1);
                  data_.append(value);
                  data_.push_back('\n');
                  if (data_.size() > kLimit)
                     throw LlmError("SSE event too large");
               }
            }
         }
         [[nodiscard]] bool completed() const noexcept { return completed_; }
         [[nodiscard]] const Value& response() const { return response_; }

      private:
         void complete_event()
         {
            if (data_.empty())
               return;
            if (data_.back() == '\n')
               data_.pop_back();
            std::string item = std::move(data_);
            data_.clear();
            if (item == "[DONE]")
               return;
            Value event;
            try {
               event = parse_json(item);
            } catch (const std::exception&) {
               throw LlmError("Malformed OpenAI stream event");
            }
            try {
               const auto kind = required_string(event, "type");
               if (kind == "response.output_text.delta" && on_delta_)
                  on_delta_(required_string(event, "delta"));
               else if (kind == "response.completed") {
                  response_ = event.at("response");
                  completed_ = true;
               } else if (kind == "response.failed" || kind == "response.incomplete" || kind == "error")
                  throw LlmError("OpenAI response failed or was incomplete");
            } catch (const ValueError&) {
               throw LlmError("Invalid OpenAI stream event fields");
            } catch (const std::out_of_range&) {
               throw LlmError("OpenAI stream event missing required fields");
            }
         }
         static constexpr std::size_t kLimit = 8 * 1024 * 1024;
         std::string buffer_;
         std::string data_;
         TextDeltaCallback on_delta_;
         bool completed_{};
         Value response_{};
      };
   } // namespace

   OpenAIResponsesProvider::OpenAIResponsesProvider(std::shared_ptr<IHttpTransport> transport,
                                                    OpenAIResponsesOptions options)
       : transport_(std::move(transport)), options_(std::move(options))
   {
      if (!transport_ || options_.api_key.empty() || options_.model.empty() || options_.max_output_tokens == 0)
         throw std::invalid_argument("OpenAI transport, API key, model and token limit are required");
      if (options_.api_key.find_first_of("\r\n") != std::string::npos)
         throw std::invalid_argument("Invalid API key");
   }

   Value OpenAIResponsesProvider::request_json(const LlmRequest& request, bool stream) const
   {
      if (request.input.empty())
         throw LlmError("Empty LLM request input");
      Value items = Value::array();
      for (const auto& item : request.input) {
         if (item.kind == LlmInputItem::Kind::UserMessage) {
            if (item.content.empty())
               throw LlmError("Empty user message");
            items.push_back(Value::object({{"role", "user"}, {"content", item.content}}));
         } else {
            if (item.call_id.empty() || !request.previous_response_id)
               throw LlmError("Tool output must reference a previous response and call_id");
            items.push_back(
               Value::object({{"type", "function_call_output"}, {"call_id", item.call_id}, {"output", item.content}}));
         }
      }
      Value api_tools = Value::array();
      if (!request.tools.is<Value::Array>())
         throw LlmError("Tool definitions must be an array");
      for (const auto& tool : request.tools.get<Value::Array>()) {
         try {
            api_tools.push_back(Value::object({{"type", "function"},
                                               {"name", tool.at("name")},
                                               {"description", tool.at("description")},
                                               {"parameters", tool.at("parameters")},
                                               // Avoid accidental normalization into strict mode: schemas
                                               // here target Draft 2020-12, with features not necessarily
                                               // supported by strict OpenAI tool schemas.
                                               {"strict", false}}));
         } catch (const std::exception&) {
            throw LlmError("Malformed tool definition");
         }
      }
      Value payload = Value::object({{"model", options_.model},
                                     {"store", true},
                                     {"stream", stream},
                                     {"input", std::move(items)},
                                     {"tools", std::move(api_tools)},
                                     {"max_output_tokens", static_cast<std::uint64_t>(options_.max_output_tokens)}});
      if (request.previous_response_id)
         payload.set("previous_response_id", *request.previous_response_id);
      return payload;
   }

   LlmResponse OpenAIResponsesProvider::parse_response(const Value& raw) const
   {
      try {
         if (!raw.is<Value::Object>() || required_string(raw, "status") != "completed")
            throw LlmError("OpenAI response not completed");
         LlmResponse result;
         result.response_id = required_string(raw, "id");
         if (!raw.at("output").is<Value::Array>())
            throw LlmError("Missing output items");
         for (const auto& item : raw.at("output").get<Value::Array>()) {
            const auto type = required_string(item, "type");
            if (type == "function_call") {
               result.tool_calls.push_back({required_string(item, "call_id"), required_string(item, "name"),
                                            required_string(item, "arguments")});
            } else if (type == "message") {
               if (!item.at("content").is<Value::Array>())
                  throw LlmError("Malformed message content");
               for (const auto& fragment : item.at("content").get<Value::Array>()) {
                  const auto subtype = required_string(fragment, "type");
                  if (subtype == "output_text")
                     result.text += required_string(fragment, "text");
                  if (subtype == "refusal")
                     result.text += required_string(fragment, "refusal");
               }
            }
            // Opaque reasoning items need not be locally replayed because
            // subsequent requests carry previous_response_id and store=true.
         }
         if (result.response_id.empty())
            throw LlmError("OpenAI response ID empty");
         return result;
      } catch (const LlmError&) {
         throw;
      } catch (const std::exception&) {
         throw LlmError("Malformed OpenAI response payload");
      }
   }

   LlmResponse OpenAIResponsesProvider::complete(const LlmRequest& request, std::stop_token stop,
                                                 TextDeltaCallback on_delta)
   {
      if (stop.stop_requested())
         throw ChatCancelled{};
      const bool streaming = options_.enable_streaming && static_cast<bool>(on_delta);
      const auto body = to_json(request_json(request, streaming));
      const std::vector<HttpHeader> headers = {{"Authorization", "Bearer " + options_.api_key},
                                               {"Content-Type", "application/json"}};
      SseParser stream_reader(on_delta);
      HttpChunkCallback chunk = streaming
                                   ? HttpChunkCallback{[&](std::string_view data) { stream_reader.append(data); }}
                                   : HttpChunkCallback{};
      const auto response = transport_->post(kEndpoint, body, headers, chunk, stop);
      if (stop.stop_requested())
         throw ChatCancelled{};
      if (response.status_code < 200 || response.status_code >= 300)
         throw LlmError("OpenAI API request failed (HTTP " + std::to_string(response.status_code) + ")");
      if (streaming) {
         if (!response.body.empty())
            stream_reader.append(response.body);
         if (!stream_reader.completed())
            throw LlmError("OpenAI stream ended without response.completed");
         return parse_response(stream_reader.response());
      }
      try {
         return parse_response(parse_json(response.body));
      } catch (const JsonError&) {
         throw LlmError("Malformed OpenAI JSON response");
      }
   }

} // namespace CxxRelay::core
