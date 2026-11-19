#include <catch2/catch_test_macros.hpp>

#include <cxxrelay/core.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

using namespace CxxRelay;

namespace
{
   struct Position
   {
      int x{};
      int y{};
   };
   struct NoDefault
   {
      explicit NoDefault(int) {}
   };

   struct Service
   {
      std::string title{"initial"};
      int calls{};

      int sum(int a, int b) const { return a + b; }
      void rename(std::string name)
      {
         title = std::move(name);
         ++calls;
      }
      const std::string& read_title() const { return title; }
      std::string echo(const std::string& str) const { return str; }
      std::string consume(std::string&& str) const { return std::move(str); }
      int count() const noexcept { return calls; }
      void clear() noexcept { title.clear(); }
      int ambiguous(int x) const { return x * 2; }
      double ambiguous(double x) const { return x * 3; }
      int ambiguous(int x) { return x * 4; }
      int only_lvalue() & { return 10; }
      int only_rvalue() && { return 20; }
      int only_const_lvalue() const& { return 30; }
      void mutate_reference(int& arg)
      {
         arg += 100;
         ++calls;
      }
      Position translate(Position p) const { return Position{p.x + 1, p.y + 2}; }
      std::vector<int> sequence(std::optional<int> n) const
      {
         return n ? std::vector<int>{*n, *n + 1} : std::vector<int>{};
      }
      void throw_exception() { throw std::runtime_error("application failure"); }
      int checked_int(std::uint8_t n) const { return n; }
      void pointer_arg(const int*) { ++calls; }
      int* pointer_return()
      {
         ++calls;
         return nullptr;
      }
      void requires_nondefault(NoDefault) { ++calls; }
   };

   TypeRegistry make_registry()
   {
      TypeRegistry reg;
      reg.type<Position>("Position").field("x", &Position::x).field("y", &Position::y);
      using ConstInt = int (Service::*)(int) const;
      using ConstDouble = double (Service::*)(double) const;
      using MutableInt = int (Service::*)(int);
      reg.type<Service>("Service")
         .method("sum", &Service::sum, {.parameter_names = {"left", "right"}})
         .method("rename", &Service::rename, {.parameter_names = {"title"}})
         .method("title", &Service::read_title)
         .method("echo", &Service::echo, {.parameter_names = {"text"}})
         .method("consume", &Service::consume)
         .method("count", &Service::count)
         .method("clear", &Service::clear)
         .method("ambiguous", static_cast<ConstInt>(&Service::ambiguous))
         .method("ambiguous", static_cast<ConstDouble>(&Service::ambiguous))
         .method("ambiguous", static_cast<MutableInt>(&Service::ambiguous))
         .method("lvalue", &Service::only_lvalue)
         .method("rvalue", &Service::only_rvalue)
         .method("const_lvalue", &Service::only_const_lvalue)
         .method("mutate_reference", &Service::mutate_reference)
         .method("translate", &Service::translate, {.parameter_names = {"position"}})
         .method("sequence", &Service::sequence, {.parameter_names = {"n"}})
         .method("throw", &Service::throw_exception)
         .method("checked_int", &Service::checked_int, {.parameter_names = {"n"}})
         .method("pointer_arg", &Service::pointer_arg)
         .method("pointer_return", &Service::pointer_return)
         .method("requires_nondefault", &Service::requires_nondefault);
      return reg;
   }

   void expect_code(const std::function<void()>& operation, ReflectionErrorCode expected)
   {
      bool seen = false;
      try {
         operation();
      } catch (const ReflectionError& e) {
         REQUIRE(e.code() == expected);
         seen = true;
      }
      REQUIRE(seen);
   }
} // namespace

TEST_CASE("4.1 - Invoke const method with positional arguments", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "sum", Value::array({2, 3})).as_int64() == 5);
}
TEST_CASE("4.2 - Invoke const method on const object", "[invoke]")
{
   auto reg = make_registry();
   const Service obj;
   REQUIRE(reg.invoke(obj, "sum", Value::array({4, 6})).as_int64() == 10);
}
TEST_CASE("4.3 - Mutable method modifies original object", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "rename", Value::array({"new"})).is_null());
   REQUIRE(obj.title == "new");
   REQUIRE(obj.calls == 1);
}
TEST_CASE("4.4 - Void method returns null and no-argument default is supported", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "clear").is_null());
   REQUIRE(obj.title.empty());
}
TEST_CASE("4.5 - Const noexcept method works", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "count").as_int64() == 0);
}
TEST_CASE("4.6 - Returned const reference becomes an owned Value", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   Value result = reg.invoke(obj, "title");
   obj.title = "changed";
   REQUIRE(result.get<std::string>() == "initial");
}
TEST_CASE("4.7 - const reference arguments bind to decoded local", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "echo", Value::array({"abc"})).get<std::string>() == "abc");
}
TEST_CASE("4.8 - Rvalue reference arguments bind to decoded temporary", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "consume", Value::array({"abc"})).get<std::string>() == "abc");
}
TEST_CASE("4.9 - Named arguments use registered names irrespective of key ordering", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "sum", Value::object({{"right", 4}, {"left", 9}})).as_int64() == 13);
}
TEST_CASE("4.10 - Unknown method is reported", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "unknown"); }, ReflectionErrorCode::UnknownMethod);
}
TEST_CASE("4.11 - Unknown object type is reported", "[invoke]")
{
   TypeRegistry reg;
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "sum"); }, ReflectionErrorCode::UnregisteredType);
}
TEST_CASE("4.12 - Overloaded name requires explicit signature", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "ambiguous", Value::array({3})); }, ReflectionErrorCode::AmbiguousMethod);
}
TEST_CASE("4.13 - Explicit overloaded const integer signature", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   using Sig = int (Service::*)(int) const;
   REQUIRE(reg.invoke(obj, "ambiguous", Value::array({3}), typeid(Sig)).as_int64() == 6);
}
TEST_CASE("4.14 - Explicit overloaded floating-point signature", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   using Sig = double (Service::*)(double) const;
   REQUIRE(reg.invoke(obj, "ambiguous", Value::array({2.5}), typeid(Sig)).get<double>() == 7.5);
}
TEST_CASE("4.15 - Explicit overloaded mutable signature", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   using Sig = int (Service::*)(int);
   REQUIRE(reg.invoke(obj, "ambiguous", Value::array({3}), typeid(Sig)).as_int64() == 12);
}
TEST_CASE("4.16 - Unknown signature is rejected", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "sum", Value::array({1, 2}), typeid(void (*)())); },
               ReflectionErrorCode::UnknownMethod);
}
TEST_CASE("4.17 - Non-const method cannot run on const instance", "[invoke]")
{
   auto reg = make_registry();
   const Service obj;
   expect_code([&] { (void)reg.invoke(obj, "rename", Value::array({"new"})); }, ReflectionErrorCode::ConstViolation);
}
TEST_CASE("4.18 - Ref-qualified lvalue methods reject rvalue targets", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.invoke(obj, "lvalue").as_int64() == 10);
   expect_code([&] { (void)reg.invoke(std::move(obj), "lvalue"); }, ReflectionErrorCode::RefQualifierMismatch);
}
TEST_CASE("4.19 - Ref-qualified rvalue methods reject lvalue targets", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "rvalue"); }, ReflectionErrorCode::RefQualifierMismatch);
   REQUIRE(reg.invoke(std::move(obj), "rvalue").as_int64() == 20);
}
TEST_CASE("4.20 - const lvalue qualified method on const instance", "[invoke]")
{
   auto reg = make_registry();
   const Service obj;
   REQUIRE(reg.invoke(obj, "const_lvalue").as_int64() == 30);
}
TEST_CASE("4.21 - Mutable reference parameters cannot discard output changes", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "mutate_reference", Value::array({42})); },
               ReflectionErrorCode::UnsupportedParameter);
   REQUIRE(obj.calls == 0);
}
TEST_CASE("4.22 - Wrong number of positional parameters fails", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "sum", Value::array({1})); }, ReflectionErrorCode::InvalidArguments);
   expect_code([&] { (void)reg.invoke(obj, "sum", Value::array({1, 2, 3})); }, ReflectionErrorCode::InvalidArguments);
}
TEST_CASE("4.23 - Missing named argument fails", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "sum", Value::object({{"left", 1}})); },
               ReflectionErrorCode::MissingArgument);
}
TEST_CASE("4.24 - Unexpected named argument fails", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "sum", Value::object({{"left", 1}, {"right", 2}, {"other", 9}})); },
               ReflectionErrorCode::UnknownArgument);
}
TEST_CASE("4.25 - Argument container must be array or object", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "sum", Value(nullptr)); }, ReflectionErrorCode::InvalidArguments);
}
TEST_CASE("4.26 - Invalid argument type reports TypeMismatch without invoking", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "rename", Value::array({123})); }, ReflectionErrorCode::TypeMismatch);
   REQUIRE(obj.calls == 0);
}
TEST_CASE("4.27 - Out of range numeric arguments are rejected", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "checked_int", Value::array({1000})); }, ReflectionErrorCode::OutOfRange);
}
TEST_CASE("4.28 - Registered nested C++ object arguments and return values", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   Value input = Value::object({{"x", 4}, {"y", 8}});
   Value output = reg.invoke(obj, "translate", Value::object({{"position", input}}));
   REQUIRE(output.at("x").as_int64() == 5);
   REQUIRE(output.at("y").as_int64() == 10);
}
TEST_CASE("4.29 - Vector and optional values are converted through codecs", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   auto values = reg.invoke(obj, "sequence", Value::array({4}));
   REQUIRE(values.get<Value::Array>().size() == 2);
   REQUIRE(values.at(0).as_int64() == 4);
   REQUIRE(values.at(1).as_int64() == 5);
   REQUIRE(reg.invoke(obj, "sequence", Value::array({nullptr})).get<Value::Array>().empty());
}
TEST_CASE("4.30 - Target exceptions are distinguishable from conversion errors", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   bool caught = false;
   try {
      (void)reg.invoke(obj, "throw");
   } catch (const ReflectionError& error) {
      caught = true;
      REQUIRE(error.code() == ReflectionErrorCode::InvocationFailed);
      try {
         std::rethrow_if_nested(error);
      } catch (const std::runtime_error& original) {
         REQUIRE(std::string(original.what()) == "application failure");
      }
   }
   REQUIRE(caught);
}
TEST_CASE("4.31 - Changes to argument Value do not modify decoded string reference", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   Value values = Value::array({"original"});
   REQUIRE(reg.invoke(obj, "echo", values).get<std::string>() == "original");
   values.at(0) = "changed";
   REQUIRE(reg.invoke(obj, "echo", values).get<std::string>() == "changed");
}
TEST_CASE("4.32 - Access metadata is not a permission check", "[invoke]")
{
   TypeRegistry reg;
   reg.type<Service>("Service").method("rename", &Service::rename,
                                       {.parameter_names = {"title"}, .access = MethodAccess::ReadOnly});
   Service obj;
   REQUIRE(reg.find<Service>()->find_method("rename")->access() == MethodAccess::ReadOnly);
   REQUIRE(reg.invoke(obj, "rename", Value::object({{"title", "new"}})).is_null());
   REQUIRE(obj.title == "new");
}

TEST_CASE("4.33 - Pointer parameters remain valid metadata but cannot be invoked", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.find<Service>()->find_method("pointer_arg") != nullptr);
   expect_code([&] { (void)reg.invoke(obj, "pointer_arg", Value::array({0})); },
               ReflectionErrorCode::UnsupportedParameter);
   REQUIRE(obj.calls == 0);
}
TEST_CASE("4.34 - Pointer return types remain valid metadata but cannot be encoded", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   expect_code([&] { (void)reg.invoke(obj, "pointer_return"); }, ReflectionErrorCode::UnsupportedReturn);
   REQUIRE(obj.calls == 0);
}
TEST_CASE("4.35 - Non-default constructible parameters remain valid metadata", "[invoke]")
{
   auto reg = make_registry();
   Service obj;
   REQUIRE(reg.find<Service>()->find_method("requires_nondefault") != nullptr);
   expect_code([&] { (void)reg.invoke(obj, "requires_nondefault", Value::array({nullptr})); },
               ReflectionErrorCode::UnsupportedParameter);
   REQUIRE(obj.calls == 0);
}
