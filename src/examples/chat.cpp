#include <cxxrelay/chat.h>

#include <iostream>
#include <memory>
#include <utility>

using namespace CxxRelay;

struct DocumentService
{
   std::string title = "Draft";
   std::string search(std::string query) const { return "Found: " + query; }
   void rename(std::string value) { title = std::move(value); }
};

// A deterministic scripted provider exercises the entire workflow offline.
class ScriptedProvider final : public ILLMProvider
{
public:
   LlmResponse complete(const LlmRequest& request, std::stop_token, TextDeltaCallback on_delta) override
   {
      if (request.previous_response_id) {
         for (const auto& item : request.input)
            if (item.kind == LlmInputItem::Kind::ToolOutput)
               std::cout << "Tool output " << item.call_id << ": " << item.content << '\n';
      }
      switch (step_++) {
      case 0:
         return {"resp_1", "", {{"call_1", "documents_search", R"({"query":"report"})"}}};
      case 1: {
         if (on_delta)
            on_delta("Found the document. ");
         return {"resp_2", "", {{"call_2", "documents_rename", R"({"title":"Final report"})"}}};
      }
      case 2: {
         if (on_delta)
            on_delta("The document has been renamed.");
         return {"resp_3", "The document has been renamed.", {}};
      }
      default:
         throw LlmError("Scripted demo exhausted");
      }
   }

private:
   int step_{};
};

int main()
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("search", &DocumentService::search, {.parameter_names = {"query"}, .description = "Search documents"})
      .method("rename", &DocumentService::rename, {.parameter_names = {"title"}, .description = "Rename document"});
   auto documents = std::make_shared<DocumentService>();
   ToolDispatcher tools(registry);
   tools.expose("documents_search", documents, "search").expose("documents_rename", documents, "rename");

   ScriptedProvider provider;
   ChatOrchestrator chat(provider, tools);
   const ToolContext context{.principal = "demo", .allow_read = true, .allow_write = true};
   auto on_delta = [](std::string_view text) { std::cout << "[stream] " << text << '\n'; };
   auto result = chat.send("Find and rename the report", context, {}, on_delta);
   if (result.status == ChatTurnResult::Status::AwaitingConfirmation) {
      std::cout << "Approval required: " << result.approval->tool_name << ' ' << to_json(result.approval->arguments)
                << '\n';
      // This is a scripted DEMO approval, not suitable for production!
      // Real GUI: show these exact arguments and collect user approval.
      result = chat.resume(result.approval->approval_id, true, context, {}, on_delta);
   }
   std::cout << "Chat response: " << result.answer << '\n';
   std::cout << "Document title: " << documents->title << '\n';
}
