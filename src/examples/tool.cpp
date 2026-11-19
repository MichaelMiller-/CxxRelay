#include <cxxrelay/tool_dispatcher.h>

#include <iostream>
#include <memory>
#include <string>

using namespace CxxRelay;

struct DocumentService
{
   std::string current_title = "Draft";
   std::string search(std::string query) const { return "Found: " + query; }
   void rename(std::string title) { current_title = std::move(title); }
};

int main()
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("search", &DocumentService::search, {.parameter_names = {"query"}, .description = "Search for documents"})
      .method("rename", &DocumentService::rename,
              {.parameter_names = {"title"}, .description = "Rename the current document"});

   auto service = std::make_shared<DocumentService>();
   ToolDispatcher tools(registry);
   tools.expose("documents_search", service, "search").expose("documents_rename", service, "rename");

   ToolContext viewer{.principal = "demo-user", .allow_read = true};
   std::cout << "Visible read tools: " << to_json(tools.definitions(viewer)) << '\n';
   std::cout << tools.dispatch("documents_search", R"({"query":"report"})", viewer).to_json() << '\n';

   ToolContext editor{.principal = "demo-user", .allow_read = true, .allow_write = true};
   std::cout << "Before approval: "
             << tools.dispatch("documents_rename", R"({"title":"Final report"})", editor).to_json() << '\n';
   // In a GUI this callback must obtain approval from the human user,
   // displaying the EXACT immutable tool call, not asking the LLM.
   editor.confirm_write = [](const ToolConfirmationRequest& request) {
      std::cout << "Trusted UI approves: " << request.tool_name << " " << to_json(request.arguments) << '\n';
      return true; // Demo ONLY. Never unconditionally approve in production.
   };
   std::cout << "After approval: "
             << tools.dispatch("documents_rename", R"({"title":"Final report"})", editor).to_json() << '\n';
   std::cout << "Current title: " << service->current_title << '\n';
}
