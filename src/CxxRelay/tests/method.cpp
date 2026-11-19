#include <catch2/catch_test_macros.hpp>

#include <cxxrelay/core.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

using namespace CxxRelay;

namespace
{
   struct DocumentService
   {
      std::string title{};
      std::int32_t counter{};

      [[nodiscard]] std::string search(const std::string& query, std::uint64_t limit) const
      {
         return query + std::to_string(limit);
      }
      void rename(std::uint64_t id, const std::string& next)
      {
         (void)id;
         title = next;
      }
      [[nodiscard]] std::size_t size() const noexcept { return title.size(); }
      void reset() noexcept { title.clear(); }
      [[nodiscard]] int add(int x, int y) const { return x + y; }
      [[nodiscard]] int multiply(int x, int y) { return x * y; }
      [[nodiscard]] int overloaded(int x) const { return x; }
      [[nodiscard]] double overloaded(double x) const { return x; }
      [[nodiscard]] int overloaded(int x) { return -x; }
      [[nodiscard]] const std::string& title_ref() const { return title; }
      [[nodiscard]] std::string&& take_title() && { return std::move(title); }
      [[nodiscard]] int by_lvalue() & { return counter; }
      [[nodiscard]] int by_const_lvalue() const& noexcept { return counter; }
      void modify(int& value, const double& ratio, std::string&& text)
      {
         value += static_cast<int>(ratio) + static_cast<int>(text.size());
      }
      [[nodiscard]] std::vector<int> numbers(const std::optional<std::string>& query) const
      {
         return query ? std::vector<int>{1} : std::vector<int>{};
      }
      void set_counter(int v) { counter = v; }
      void set_other_counter(int v) { counter = v; }
   };

   struct OtherService
   {
      int get() const { return 0; }
   };

   void expect_code(const std::function<void()>& operation, ReflectionErrorCode code)
   {
      bool caught = false;
      try {
         operation();
      } catch (const ReflectionError& error) {
         caught = true;
         REQUIRE(error.code() == code);
      }
      REQUIRE(caught);
   }

} // namespace

TEST_CASE("3.1 - Registered type starts with no methods", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   (void)builder;
   const auto* descriptor = registry.find<DocumentService>();
   REQUIRE(descriptor != nullptr);
   REQUIRE(descriptor->methods().empty());
   REQUIRE(descriptor->find_method("missing") == nullptr);
   REQUIRE(descriptor->find_methods("missing").empty());
}

TEST_CASE("3.2 - Const method exposes return and parameter metadata", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("search", &DocumentService::search,
              {.parameter_names = {"query", "limit"}, .description = "Search for documents"});
   const MethodDescriptor* method = registry.find<DocumentService>()->find_method("search");
   REQUIRE(method != nullptr);
   REQUIRE(method->name() == "search");
   REQUIRE(method->description() == "Search for documents");
   REQUIRE(method->access() == MethodAccess::ReadOnly);
   REQUIRE(method->return_cpp_type() == std::type_index(typeid(std::string)));
   REQUIRE_FALSE(method->returns_void());
   REQUIRE(method->return_passing() == PassingMode::Value);
   REQUIRE(method->is_const());
   REQUIRE_FALSE(method->is_noexcept());
   REQUIRE(method->ref_qualifier() == RefQualifier::None);
   REQUIRE(method->parameters().size() == 2);
   REQUIRE(method->parameters()[0].name() == "query");
   REQUIRE(method->parameters()[0].cpp_type() == typeid(std::string));
   REQUIRE(method->parameters()[0].passing() == PassingMode::ConstLValueReference);
   REQUIRE(method->parameters()[1].name() == "limit");
   REQUIRE(method->parameters()[1].cpp_type() == typeid(std::uint64_t));
   REQUIRE(method->parameters()[1].passing() == PassingMode::Value);
}

TEST_CASE("3.3 - Mutable method carries write classification", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("rename", &DocumentService::rename, {.parameter_names = {"id", "title"}, .access = MethodAccess::Write});
   const auto* method = registry.find<DocumentService>()->find_method("rename");
   REQUIRE(method != nullptr);
   REQUIRE_FALSE(method->is_const());
   REQUIRE(method->access() == MethodAccess::Write);
   REQUIRE(method->returns_void());
   REQUIRE(method->return_cpp_type() == typeid(void));
   REQUIRE(method->parameters().size() == 2);
}

TEST_CASE("3.4 - No-argument noexcept method metadata", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService").method("size", &DocumentService::size);
   const auto* method = registry.find<DocumentService>()->find_method("size");
   REQUIRE(method != nullptr);
   REQUIRE(method->parameters().empty());
   REQUIRE(method->is_noexcept());
   REQUIRE(method->is_const());
   REQUIRE(method->return_cpp_type() == typeid(std::size_t));
}

TEST_CASE("3.5 - Default parameter labels are stable", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService").method("add", &DocumentService::add);
   const auto* method = registry.find<DocumentService>()->find_method("add");
   REQUIRE(method->parameters().size() == 2);
   REQUIRE(method->parameters()[0].name() == "arg0");
   REQUIRE(method->parameters()[1].name() == "arg1");
}

TEST_CASE("3.6 - Parameter names can be found", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("add", &DocumentService::add, {.parameter_names = {"first", "second"}});
   const auto* method = registry.find<DocumentService>()->find_method("add");
   REQUIRE(method->find_parameter("first") == &method->parameters()[0]);
   REQUIRE(method->find_parameter("second") == &method->parameters()[1]);
   REQUIRE(method->find_parameter("nope") == nullptr);
}

TEST_CASE("3.7 - Overloaded methods require disambiguation", "[method]")
{
   TypeRegistry registry;
   using IntConst = int (DocumentService::*)(int) const;
   using DoubleConst = double (DocumentService::*)(double) const;
   using IntMutable = int (DocumentService::*)(int);
   registry.type<DocumentService>("DocumentService")
      .method("overloaded", static_cast<IntConst>(&DocumentService::overloaded))
      .method("overloaded", static_cast<DoubleConst>(&DocumentService::overloaded))
      .method("overloaded", static_cast<IntMutable>(&DocumentService::overloaded));
   const auto* descriptor = registry.find<DocumentService>();
   REQUIRE(descriptor->methods().size() == 3);
   REQUIRE(descriptor->find_methods("overloaded").size() == 3);
   REQUIRE(descriptor->find_method("overloaded") == nullptr);
   REQUIRE(descriptor->find_method("overloaded", typeid(IntConst))->is_const());
   REQUIRE(descriptor->find_method("overloaded", typeid(DoubleConst))->return_cpp_type() == typeid(double));
   REQUIRE_FALSE(descriptor->find_method("overloaded", typeid(IntMutable))->is_const());
   REQUIRE(descriptor->find_method("overloaded", typeid(int (DocumentService::*)())) == nullptr);
}

TEST_CASE("3.8 - Duplicate name and signature are rejected", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   builder.method("modify", &DocumentService::set_counter);
   expect_code([&] { builder.method("modify", &DocumentService::set_counter); }, ReflectionErrorCode::DuplicateMethod);
   expect_code([&] { builder.method("modify", &DocumentService::set_other_counter); },
               ReflectionErrorCode::DuplicateMethod);
   REQUIRE(registry.find<DocumentService>()->methods().size() == 1);
}

TEST_CASE("3.9 - Different reflection names can share a signature", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("set1", &DocumentService::set_counter)
      .method("set2", &DocumentService::set_other_counter);
   const auto* descriptor = registry.find<DocumentService>();
   REQUIRE(descriptor->methods().size() == 2);
   REQUIRE(descriptor->find_method("set1") != nullptr);
   REQUIRE(descriptor->find_method("set2") != nullptr);
}

TEST_CASE("3.10 - Empty method names are rejected", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   expect_code([&] { builder.method("", &DocumentService::size); }, ReflectionErrorCode::InvalidMethodName);
   REQUIRE(registry.find<DocumentService>()->methods().empty());
}

TEST_CASE("3.11 - Incorrect parameter name count is rejected", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   expect_code([&] { builder.method("search", &DocumentService::search, {.parameter_names = {"query"}}); },
               ReflectionErrorCode::InvalidParameterNames);
   REQUIRE(registry.find<DocumentService>()->methods().empty());
}

TEST_CASE("3.12 - Duplicate parameter names are rejected", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   expect_code([&] { builder.method("add", &DocumentService::add, {.parameter_names = {"value", "value"}}); },
               ReflectionErrorCode::InvalidParameterNames);
   REQUIRE(registry.find<DocumentService>()->methods().empty());
}

TEST_CASE("3.13 - Empty parameter names are rejected", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   expect_code([&] { builder.method("add", &DocumentService::add, {.parameter_names = {"first", ""}}); },
               ReflectionErrorCode::InvalidParameterNames);
   REQUIRE(registry.find<DocumentService>()->methods().empty());
}

TEST_CASE("3.14 - Explicit names on a zero-argument method are rejected", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   expect_code([&] { builder.method("size", &DocumentService::size, {.parameter_names = {"extra"}}); },
               ReflectionErrorCode::InvalidParameterNames);
}

TEST_CASE("3.15 - Ref-qualified functions are represented", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("on_lvalue", &DocumentService::by_lvalue)
      .method("on_const_lvalue", &DocumentService::by_const_lvalue)
      .method("on_rvalue", &DocumentService::take_title);
   const auto* type = registry.find<DocumentService>();
   REQUIRE(type->find_method("on_lvalue")->ref_qualifier() == RefQualifier::LValue);
   REQUIRE_FALSE(type->find_method("on_lvalue")->is_const());
   REQUIRE(type->find_method("on_const_lvalue")->ref_qualifier() == RefQualifier::LValue);
   REQUIRE(type->find_method("on_const_lvalue")->is_const());
   REQUIRE(type->find_method("on_const_lvalue")->is_noexcept());
   REQUIRE(type->find_method("on_rvalue")->ref_qualifier() == RefQualifier::RValue);
   REQUIRE(type->find_method("on_rvalue")->return_passing() == PassingMode::RValueReference);
}

TEST_CASE("3.16 - Mutable, const and rvalue reference parameters stay distinguishable", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("modify", &DocumentService::modify, {.parameter_names = {"value", "ratio", "text"}});
   const auto params = registry.find<DocumentService>()->find_method("modify")->parameters();
   REQUIRE(params.size() == 3);
   REQUIRE(params[0].cpp_type() == typeid(int));
   REQUIRE(params[0].passing() == PassingMode::LValueReference);
   REQUIRE(params[1].cpp_type() == typeid(double));
   REQUIRE(params[1].passing() == PassingMode::ConstLValueReference);
   REQUIRE(params[2].cpp_type() == typeid(std::string));
   REQUIRE(params[2].passing() == PassingMode::RValueReference);
}

TEST_CASE("3.17 - Const reference return retains its reference classification", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService").method("title_ref", &DocumentService::title_ref);
   const auto* method = registry.find<DocumentService>()->find_method("title_ref");
   REQUIRE(method->return_cpp_type() == typeid(std::string));
   REQUIRE(method->return_passing() == PassingMode::ConstLValueReference);
}

TEST_CASE("3.18 - Complex template types preserve their type identity", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("numbers", &DocumentService::numbers, {.parameter_names = {"query"}});
   const auto* method = registry.find<DocumentService>()->find_method("numbers");
   REQUIRE(method->return_cpp_type() == typeid(std::vector<int>));
   REQUIRE(method->parameters()[0].cpp_type() == typeid(std::optional<std::string>));
   REQUIRE(method->parameters()[0].passing() == PassingMode::ConstLValueReference);
}

TEST_CASE("3.19 - Methods do not change existing Value serialization", "[method]")
{
   struct Row
   {
      int id{};
      void set(int next) { id = next; }
   };
   TypeRegistry registry;
   registry.type<Row>("Row")
      .field("id", &Row::id)
      .method("set", &Row::set, {.parameter_names = {"next"}, .access = MethodAccess::Write});
   REQUIRE(registry.to_value(Row{42}) == Value::object({{"id", 42}}));
   REQUIRE(registry.from_value<Row>(Value::object({{"id", 42}})).id == 42);
   REQUIRE(registry.find<Row>()->fields().size() == 1);
   REQUIRE(registry.find<Row>()->methods().size() == 1);
}

TEST_CASE("3.20 - TypeDescriptors remain stable after further registrations", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   (void)builder;
   const auto* descriptor = registry.find<DocumentService>();
   builder.method("size", &DocumentService::size);
   registry.type<OtherService>("OtherService").method("get", &OtherService::get);
   REQUIRE(descriptor == registry.find<DocumentService>());
   REQUIRE(descriptor->find_method("size") != nullptr);
}

TEST_CASE("3.21 - Different registries have independent method lists", "[method]")
{
   TypeRegistry first;
   TypeRegistry second;
   first.type<DocumentService>("DocumentService").method("size", &DocumentService::size);
   second.type<DocumentService>("Alternative").method("rename", &DocumentService::rename);
   REQUIRE(first.find<DocumentService>()->methods().size() == 1);
   REQUIRE(second.find<DocumentService>()->methods().size() == 1);
   REQUIRE(first.find<DocumentService>()->find_method("rename") == nullptr);
   REQUIRE(second.find<DocumentService>()->find_method("size") == nullptr);
}

TEST_CASE("3.22 - Exact signature reflects noexcept qualification", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("size", &DocumentService::size)
      .method("reset", &DocumentService::reset);
   const auto* type = registry.find<DocumentService>();
   REQUIRE(type->find_method("size")->signature_type() == typeid(decltype(&DocumentService::size)));
   REQUIRE(type->find_method("reset")->signature_type() == typeid(decltype(&DocumentService::reset)));
   REQUIRE(type->find_method("size")->signature_type() != type->find_method("reset")->signature_type());
}

TEST_CASE("3.23 - Method metadata does not invoke arbitrary application code", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService").method("set", &DocumentService::set_counter);
   DocumentService service;
   REQUIRE(service.counter == 0);
   // Step 3 only registers metadata; there is deliberately no invoke() method.
   REQUIRE(registry.find<DocumentService>()->find_method("set") != nullptr);
   REQUIRE(service.counter == 0);
}

TEST_CASE("3.24 - Method and field names may overlap", "[method]")
{
   struct Record
   {
      int id{};
      [[nodiscard]] int id_value() const { return id; }
   };
   TypeRegistry registry;
   registry.type<Record>("Record").field("id", &Record::id).method("id", &Record::id_value);
   const auto* type = registry.find<Record>();
   REQUIRE(type->find_field("id") != nullptr);
   REQUIRE(type->find_method("id") != nullptr);
}

TEST_CASE("3.25 - Null member function pointer is rejected", "[method]")
{
   TypeRegistry registry;
   auto builder = registry.type<DocumentService>("DocumentService");
   using MethodPointer = decltype(&DocumentService::size);
   MethodPointer pointer = nullptr;
   expect_code([&] { builder.method("null", pointer); }, ReflectionErrorCode::InvalidMethodPointer);
   REQUIRE(registry.find<DocumentService>()->methods().empty());
}

TEST_CASE("3.26 - Mutable methods default conservatively to Write", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("add", &DocumentService::add)
      .method("multiply", &DocumentService::multiply)
      .method("size", &DocumentService::size);
   const auto* type = registry.find<DocumentService>();
   REQUIRE(type->find_method("add")->access() == MethodAccess::ReadOnly);
   REQUIRE(type->find_method("size")->access() == MethodAccess::ReadOnly);
   REQUIRE(type->find_method("multiply")->access() == MethodAccess::Write);
}

TEST_CASE("3.27 - Explicit access classification overrides conservative default", "[method]")
{
   TypeRegistry registry;
   registry.type<DocumentService>("DocumentService")
      .method("multiply", &DocumentService::multiply, {.access = MethodAccess::ReadOnly})
      .method("search", &DocumentService::search, {.access = MethodAccess::Write});
   const auto* type = registry.find<DocumentService>();
   REQUIRE(type->find_method("multiply")->access() == MethodAccess::ReadOnly);
   REQUIRE(type->find_method("search")->access() == MethodAccess::Write);
}
