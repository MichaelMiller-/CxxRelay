#include <cxxrelay/openai_responses.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

using namespace CxxRelay;

struct DocumentService
{
   std::string title = "Quarterly report - draft";

   std::string search(std::string query) const { return "Document: '" + title + "', query: '" + query + "'"; }
   void rename(std::string new_title) { title = std::move(new_title); }
};

int main()
{
   const char* key = std::getenv("OPENAI_API_KEY");
   const char* model = std::getenv("OPENAI_MODEL");
   if (!key || !*key || !model || !*model) {
      std::cerr << "Set OPENAI_API_KEY and OPENAI_MODEL first.\n";
      return 2;
   }
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("search", &DocumentService::search,
              {.parameter_names = {"query"}, .description = "Find a document by name"})
      .method("rename", &DocumentService::rename,
              {.parameter_names = {"new_title"}, .description = "Rename the current document"});
   auto documents = std::make_shared<DocumentService>();
   ToolDispatcher tools(registry);
   tools.expose("documents_search", documents, "search").expose("documents_rename", documents, "rename");

   auto transport = std::make_shared<CurlHttpTransport>();
   OpenAIResponsesProvider provider(transport, {.api_key = key, .model = model});
   ChatOrchestrator chat(provider, tools);
   const ToolContext context{.principal = "local-console-user", .allow_read = true, .allow_write = true};

   std::cout << "Current title: " << documents->title << "\nPrompt: ";
   std::string prompt;
   if (!std::getline(std::cin, prompt) || prompt.empty())
      return 0;
   const auto on_delta = [](std::string_view s) { std::cout << s << std::flush; };
   try {
      auto result = chat.send(prompt, context, {}, on_delta);
      while (result.status == ChatTurnResult::Status::AwaitingConfirmation) {
         const auto request = *result.approval;
         std::cout << "\n\nThe assistant requests a WRITE operation:\n  " << request.tool_name << " "
                   << to_json(request.arguments) << "\nApprove this exact operation? [y/N] " << std::flush;
         std::string reply;
         std::getline(std::cin, reply);
         const bool approved = reply == "y" || reply == "Y";
         result = chat.resume(request.approval_id, approved, context, {}, on_delta);
      }
      std::cout << "\n\nFinal response: " << result.answer << "\nDocument title: " << documents->title << '\n';
   } catch (const std::exception& error) {
      // Do not log raw requests, tokens or credentials here.
      std::cerr << "Chat request failed: " << error.what() << '\n';
      return 1;
   }
}
