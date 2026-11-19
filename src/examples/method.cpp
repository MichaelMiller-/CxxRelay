#include <cxxrelay/core.h>

#include <cstdint>
#include <iostream>
#include <string>

using namespace CxxRelay;

struct DocumentService
{
   std::string search(const std::string& query, std::uint64_t limit) const
   {
      return query + ":" + std::to_string(limit);
   }
   void rename(std::uint64_t id, const std::string& title)
   {
      (void)id;
      (void)title;
   }
   [[nodiscard]] std::size_t size() const noexcept { return 42; }
};

int main()
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("search", &DocumentService::search,
              {.parameter_names = {"query", "limit"}, .description = "Search documents using a text expression"})
      .method(
         "rename", &DocumentService::rename,
         {.parameter_names = {"id", "title"}, .description = "Change a document title", .access = MethodAccess::Write})
      .method("size", &DocumentService::size);

   const TypeDescriptor* type = registry.find<DocumentService>();
   if (!type || type->methods().size() != 3)
      return 1;

   for (const MethodDescriptor& method : type->methods()) {
      std::cout << type->name() << "." << method.name() << "(";
      for (std::size_t i = 0; i < method.parameters().size(); ++i) {
         if (i != 0)
            std::cout << ", ";
         std::cout << method.parameters()[i].name();
      }
      std::cout << ") [const=" << method.is_const() << ", noexcept=" << method.is_noexcept() << "]\n";
   }

   // Step 3 describes methods; the dynamic Value-based invoker is Step 4.
   const auto* rename = type->find_method("rename");
   return rename && rename->access() == MethodAccess::Write ? 0 : 2;
}
