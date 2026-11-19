#include <cxxrelay/core.h>

#include <cstdint>
#include <iostream>
#include <string>

using namespace CxxRelay;

struct DocumentService
{
   std::string title{"Untitled"};

   std::string search(const std::string& query, std::uint64_t limit) const
   {
      return "Search: " + query + " (limit " + std::to_string(limit) + ")";
   }
   void rename(const std::string& new_title) { title = new_title; }
   const std::string& current_title() const { return title; }
};

int main()
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("search", &DocumentService::search, {.parameter_names = {"query", "limit"}})
      .method("rename", &DocumentService::rename, {.parameter_names = {"new_title"}})
      .method("current_title", &DocumentService::current_title);

   DocumentService service;
   const Value result =
      registry.invoke(service, "search", Value::object({{"query", "report"}, {"limit", std::uint64_t{10}}}));
   std::cout << result.get<std::string>() << '\n';

   const Value previous_title = registry.invoke(service, "current_title");
   (void)registry.invoke(service, "rename", Value::array({"Annual Report"}));
   std::cout << "Previous: " << previous_title.get<std::string>() << '\n';
   std::cout << "Current: " << service.title << '\n';
}
