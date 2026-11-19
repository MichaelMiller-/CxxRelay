#pragma once

#include <cxxrelay/chat.h>

// Decorator lets the test UI observe the model -> tool decision without
// changing reflection-core or giving the model any additional permissions.
class TracingProvider final : public CxxRelay::ILLMProvider
{
   ILLMProvider& inner_;
   std::function<void(std::string_view, CxxRelay::Value)> notify_;

public:
   TracingProvider(ILLMProvider& inner, std::function<void(std::string_view, CxxRelay::Value)> notify)
       : inner_(inner), notify_(std::move(notify))
   {
   }

   CxxRelay::LlmResponse complete(const CxxRelay::LlmRequest& request, std::stop_token stop,
                                    CxxRelay::TextDeltaCallback on_delta) override
   {
      notify_("model_request",
         CxxRelay::Value::object({{"input_count", static_cast<std::uint64_t>(request.input.size())},
                                              {"available_tools",
                                               static_cast<std::uint64_t>(request.tools.get<CxxRelay::Value::Array>().size())}}));
      auto response = inner_.complete(request, stop, std::move(on_delta));
      for (const auto& call : response.tool_calls) {
         notify_("tool_requested",
                 CxxRelay::Value::object(
                    {{"call_id", call.call_id}, {"name", call.name}, {"arguments_json", call.arguments_json}}));
      }
      return response;
   }
};