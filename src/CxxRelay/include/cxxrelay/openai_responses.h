#pragma once

#include <cxxrelay/chat.h>

#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace CxxRelay
{
   struct HttpHeader
   {
      std::string name;
      std::string value;
   };
   struct HttpResponse
   {
      long status_code{};
      std::string body;
   };
   using HttpChunkCallback = std::function<void(std::string_view)>;

   // Inject transport to keep provider independently testable without network.
   // Implementations must honor the stop token, bound response sizes, use TLS,
   // and NEVER log the Authorization header.
   class IHttpTransport
   {
   public:
      virtual ~IHttpTransport() = default;
      virtual HttpResponse post(std::string_view url, std::string_view json, const std::vector<HttpHeader>& headers,
                                const HttpChunkCallback& chunk, std::stop_token stop) = 0;
   };

   struct OpenAIResponsesOptions
   {
      std::string api_key; // Provided by trusted host, e.g. environment; never hardcode.
      std::string model;   // Explicit model ID selected by the developer.
      std::size_t max_output_tokens{2048};
      bool enable_streaming{true};
   };

   // Stateful Responses API with previous_response_id and store=true.
   // Explicit data-retention tradeoff: previous responses are stored remotely.
   // For store=false a future adapter must replay all response items (including
   // reasoning) in correct order; only sending user and tool outputs is NOT enough.
   class OpenAIResponsesProvider final : public ILLMProvider
   {
   public:
      OpenAIResponsesProvider(std::shared_ptr<IHttpTransport> transport, OpenAIResponsesOptions options);
      LlmResponse complete(const LlmRequest& request, std::stop_token stop = {},
                           TextDeltaCallback on_delta = {}) override;

   private:
      [[nodiscard]] Value request_json(const LlmRequest& request, bool stream) const;
      [[nodiscard]] LlmResponse parse_response(const Value& raw) const;
      std::shared_ptr<IHttpTransport> transport_;
      OpenAIResponsesOptions options_;
   };

   // Optional real network transport built as reflection_openai_curl if libcurl
   // development files are found. No libcurl dependency leaks into core.
   class CurlHttpTransport final : public IHttpTransport
   {
   public:
      HttpResponse post(std::string_view url, std::string_view json, const std::vector<HttpHeader>& headers,
                        const HttpChunkCallback& chunk, std::stop_token stop) override;
   };
} // namespace CxxRelay
