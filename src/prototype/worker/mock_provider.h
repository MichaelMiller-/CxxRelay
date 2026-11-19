#pragma once

#include <cxxrelay/chat.h>

#include <algorithm>

namespace detail
{
   inline std::string lower(std::string input)
   {
      std::ranges::transform(input, input.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return input;
   }

   inline std::string trim(std::string value)
   {
      auto non_space = [](unsigned char c) { return !std::isspace(c); };
      value.erase(value.begin(), std::ranges::find_if(value, non_space));
      value.erase(std::find_if(value.rbegin(), value.rend(), non_space).base(), value.end());
      return value;
   }
} // namespace detail

// Deterministic *test double*: no model or semantic understanding.
// It merely demonstrates the very same dispatcher, approval, and chat paths.
class MockProvider final : public CxxRelay::ILLMProvider
{
public:
   CxxRelay::LlmResponse complete(const CxxRelay::LlmRequest& request, std::stop_token stop,
                                  CxxRelay::TextDeltaCallback on_delta) override
   {
      if (stop.stop_requested())
         throw CxxRelay::ChatCancelled{};
      const auto response_id = "mock_response_" + std::to_string(++counter_);
      const auto& input = request.input.at(0);
      if (input.kind == CxxRelay::LlmInputItem::Kind::ToolOutput) {
         std::string answer;
         for (const auto& item : request.input) {
            auto output = CxxRelay::parse_json(item.content);
            if (output.at("ok").get<bool>()) {
               const auto& data = output.at("result");
               if (data.is<CxxRelay::Value::Array>()) {
                  const auto& rows = data.get<CxxRelay::Value::Array>();
                  answer += std::to_string(rows.size()) + " Dokument(e):\n";
                  for (const auto& row : rows)
                     answer += "• " + std::to_string(row.at("id").as_uint64()) + ": " +
                               row.at("title").get<std::string>() + "\n";
               } else if (data.is<bool>()) {
                  answer += data.get<bool>() ? "Das Dokument wurde umbenannt.\n" : "Kein Dokument umbenannt.\n";
               } else {
                  answer += to_json(data) + "\n";
               }
            } else {
               answer += "Tool-Ergebnis: " + output.at("error").at("code").get<std::string>() + "\n";
            }
         }
         if (on_delta) {
            const auto half = answer.size() / 2;
            on_delta(std::string_view(answer).substr(0, half));
            on_delta(std::string_view(answer).substr(half));
         }
         return {response_id, answer, {}};
      }
      const auto message = detail::trim(input.content);
      const auto normalized = detail::lower(message);
      // A deliberately slow mock response makes Stop / reset testable without
      // networking. No write tools are involved in this synthetic scenario.
      if (normalized.find("langsam") != std::string::npos) {
         std::string text = "Langer Testlauf beendet.";
         for (int i = 0; i < 30; ++i) {
            if (stop.stop_requested())
               throw CxxRelay::ChatCancelled{};
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
         }
         if (on_delta)
            on_delta(text);
         return {response_id, text, {}};
      }
      const auto call_id = "mock_call_" + std::to_string(counter_);
      if (normalized.find("benenne") != std::string::npos || normalized.find("umbenenn") != std::string::npos ||
          normalized.starts_with("rename ")) {
         std::uint64_t id = 1;
         auto number_pos = normalized.find_first_of("0123456789");
         if (number_pos != std::string::npos) {
            auto end = number_pos;
            while (end < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[end])))
               ++end;
            try {
               id = std::stoull(normalized.substr(number_pos, end - number_pos));
            } catch (const std::exception&) {
               id = 1;
            }
         }
         std::size_t title_at = normalized.find(" um in ");
         std::size_t advance = 7;
         if (title_at == std::string::npos) {
            title_at = normalized.find(" in ");
            advance = 4;
         }
         if (title_at == std::string::npos) {
            title_at = normalized.find(" zu ");
            advance = 4;
         }
         std::string title =
            title_at == std::string::npos ? "Neuer Titel" : detail::trim(message.substr(title_at + advance));
         if (!title.empty() && title.back() == '.')
            title.pop_back();
         return {response_id,
                 {},
                 {{call_id, "documents_rename",
                   CxxRelay::to_json(CxxRelay::Value::object({{"id", id}, {"title", title}}))}}};
      }
      if (normalized.find("such") != std::string::npos || normalized.starts_with("search ")) {
         const auto after = normalized.find("nach ");
         std::string query = after == std::string::npos ? "Bericht" : detail::trim(message.substr(after + 5));
         return {response_id,
                 {},
                 {{call_id, "documents_search", CxxRelay::to_json(CxxRelay::Value::object({{"query", query}}))}}};
      }
      if (normalized.find("liste") != std::string::npos || normalized.find("zeig") != std::string::npos ||
          normalized.find("alle") != std::string::npos) {
         return {response_id, {}, {{call_id, "documents_list", "{}"}}};
      }
      const std::string answer = "Demo-Modus: Versuche 'Liste alle Dokumente', "
                                 "'Suche nach Bericht' oder "
                                 "'Benenne Dokument 1 um in Abschlussbericht'.";
      if (on_delta)
         on_delta(answer);
      return {response_id, answer, {}};
   }

private:
   std::size_t counter_{};
};
