#include <catch2/catch_test_macros.hpp>

#include <cxxrelay/json.h>
#include <cxxrelay/json_schema.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace CxxRelay;

namespace
{
   struct Point
   {
      double x{};
      double y{};
   };
   struct Node
   {
      std::uint64_t id{};
      Point at{};
      std::optional<std::string> label{};
   };
   struct Recursive
   {
      std::vector<Recursive> children{};
   };
   struct Missing
   {
      int x{};
   };
   struct Service
   {
      Node get(std::uint64_t id, std::optional<std::string> label) const { return {id, {1.0, 2.0}, std::move(label)}; }
      void change(std::string) {}
      void reference(int&) {}
      void unsupported(int*) {}
      int* bad_result() { return nullptr; }
      int overloaded(int) const { return 1; }
      double overloaded(double) const { return 1.0; }
   };
   TypeRegistry create_registry()
   {
      TypeRegistry registry;
      registry.type<Point>("Point").field("x", &Point::x).field("y", &Point::y);
      registry.type<Node>("Node").field("id", &Node::id).field("at", &Node::at).field("label", &Node::label);
      registry.type<Recursive>("Recursive").field("children", &Recursive::children);
      using IntSignature = int (Service::*)(int) const;
      using DoubleSignature = double (Service::*)(double) const;
      registry.type<Service>("Service")
         .method("get", &Service::get, {.parameter_names = {"id", "label"}, .description = "Get node"})
         .method("change", &Service::change, {.parameter_names = {"name"}})
         .method("reference", &Service::reference)
         .method("unsupported", &Service::unsupported)
         .method("bad_result", &Service::bad_result)
         .method("overloaded", static_cast<IntSignature>(&Service::overloaded))
         .method("overloaded", static_cast<DoubleSignature>(&Service::overloaded));
      return registry;
   }
   void json_error(std::string_view source, JsonErrorCode expected, JsonOptions opts = {})
   {
      bool got = false;
      try {
         (void)parse_json(source, opts);
      } catch (const JsonError& err) {
         REQUIRE(err.code() == expected);
         got = true;
      }
      REQUIRE(got);
   }
   void schema_error(const std::function<void()>& run, SchemaErrorCode expected)
   {
      bool got = false;
      try {
         run();
      } catch (const SchemaError& err) {
         REQUIRE(err.code() == expected);
         got = true;
      }
      REQUIRE(got);
   }
} // namespace

TEST_CASE("5.1 - JSON primitives round-trip", "[json]")
{
   REQUIRE(parse_json("null").is_null());
   REQUIRE(parse_json("true").get<bool>());
   REQUIRE_FALSE(parse_json("false").get<bool>());
   REQUIRE(parse_json("-42").as_int64() == -42);
   REQUIRE(parse_json("42").as_uint64() == 42);
   REQUIRE(parse_json("2.25").get<double>() == 2.25);
   REQUIRE(parse_json("\"hello\"").get<std::string>() == "hello");
}
TEST_CASE("5.2 - Nested objects, arrays, nulls and unicode", "[json]")
{
   const auto value = parse_json(R"({"list":[42,null,true,{"u":"Grüße"}]})");
   REQUIRE(value.at("list").size() == 4);
   REQUIRE(value.at("list").at(std::size_t{3}).at("u").get<std::string>() == "Grüße");
   REQUIRE(parse_json(to_json(value)) == value);
}
TEST_CASE("5.3 - JSON strings escape control codes", "[json]")
{
   const Value v(std::string("\"\\\n\r\t\x01", 6));
   REQUIRE(parse_json(to_json(v)) == v);
   REQUIRE(to_json(v).find("\\u0001") != std::string::npos);
}
TEST_CASE("5.4 - UTF-16 surrogate pair decoded as UTF-8", "[json]")
{
#ifndef _MSC_VER
   REQUIRE(parse_json(R"("\uD83D\uDE00")").get<std::string>() == "😀");
#endif
   REQUIRE(parse_json(R"("\u00E4")").get<std::string>() == "ä");
}
TEST_CASE("5.5 - Strict JSON rejects trailing garbage and comments", "[json]")
{
   json_error("true false", JsonErrorCode::InvalidSyntax);
   json_error("/* hi */ null", JsonErrorCode::InvalidSyntax);
   json_error("[1,]", JsonErrorCode::InvalidSyntax);
   json_error("{\"x\":1,}", JsonErrorCode::InvalidSyntax);
}
TEST_CASE("5.6 - Duplicate object keys are rejected", "[json]")
{
   json_error(R"({"x":1,"x":2})", JsonErrorCode::DuplicateKey);
   json_error(R"({"a":1,"\u0061":3})", JsonErrorCode::DuplicateKey);
}
TEST_CASE("5.7 - Malformed numeric tokens are rejected", "[json]")
{
   for (const auto& s : {"01", "-01", "1.", "1e", "1e+", "--1", "1e9999"})
      json_error(s, JsonErrorCode::InvalidNumber);
}
TEST_CASE("5.8 - Exact 64-bit integral boundaries", "[json]")
{
   REQUIRE(parse_json("18446744073709551615").get<std::uint64_t>() == std::numeric_limits<std::uint64_t>::max());
   REQUIRE(parse_json("-9223372036854775808").get<std::int64_t>() == std::numeric_limits<std::int64_t>::min());
   json_error("18446744073709551616", JsonErrorCode::InvalidNumber);
   json_error("-9223372036854775809", JsonErrorCode::InvalidNumber);
}
TEST_CASE("5.9 - Preserve double kind and negative zero", "[json]")
{
   REQUIRE(parse_json(to_json(Value(1.0))).is<double>());
   REQUIRE(parse_json(to_json(Value(-0.0))).is<double>());
   REQUIRE(parse_json("1e2").get<double>() == 100.0);
}
TEST_CASE("5.10 - Unsigned small numbers lose kind in standard JSON", "[json]")
{
   const Value input(std::uint64_t{42});
   REQUIRE(to_json(input) == "42");
   REQUIRE(parse_json(to_json(input)).is<std::int64_t>());
   REQUIRE(parse_json(to_json(input)).as_uint64() == 42);
}
TEST_CASE("5.11 - Invalid unicode is rejected", "[json]")
{
   json_error(R"("\uD800")", JsonErrorCode::InvalidUnicode);
   json_error(R"("\uDC00")", JsonErrorCode::InvalidUnicode);
   json_error(R"("\uD800\u0041")", JsonErrorCode::InvalidUnicode);
   json_error(R"("\z")", JsonErrorCode::InvalidSyntax);
   json_error(std::string("\"\xC0\xAF\"", 4), JsonErrorCode::InvalidUnicode);
   REQUIRE_THROWS_AS(to_json(Value(std::string("\x80", 1))), JsonError);
}
TEST_CASE("5.12 - Reject raw newlines in strings", "[json]") { json_error("\"a\nb\"", JsonErrorCode::InvalidSyntax); }
TEST_CASE("5.13 - Parser input size and nesting limits", "[json]")
{
   json_error("[1,2,3]", JsonErrorCode::ResourceLimit, JsonOptions{128, 4, 100});
   json_error("[[1]]", JsonErrorCode::ResourceLimit, JsonOptions{1, 100, 100});
   json_error("[1,2,3]", JsonErrorCode::ResourceLimit, JsonOptions{10, 100, 3});
}
TEST_CASE("5.14 - Writer output and nesting limits", "[json]")
{
   REQUIRE_THROWS_AS(to_json(Value::array({1, 2, 3}), JsonOptions{128, 4, 100}), JsonError);
   REQUIRE_THROWS_AS(to_json(Value::array({Value::array({1})}), JsonOptions{1, 100, 100}), JsonError);
   REQUIRE_THROWS_AS(to_json(Value::array({1, 2, 3}), JsonOptions{10, 100, 3}), JsonError);
}
TEST_CASE("5.15 - Schema primitive ranges reflect actual C++ storage", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   auto schema = gen.type<std::uint8_t>();
   REQUIRE(schema.at("type").get<std::string>() == "integer");
   REQUIRE(schema.at("minimum").as_uint64() == 0);
   REQUIRE(schema.at("maximum").as_uint64() == 255);
   REQUIRE(gen.type<double>().at("type").get<std::string>() == "number");
   REQUIRE(gen.type<std::string>().at("type").get<std::string>() == "string");
   REQUIRE(gen.type<Value>().contains("$schema"));
}
TEST_CASE("5.16 - Struct schema uses $defs and $ref", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   auto schema = gen.type<Node>();
   REQUIRE(schema.at("$ref").get<std::string>() == "#/$defs/Node");
   REQUIRE(schema.at("$defs").contains("Point"));
   REQUIRE(schema.at("$defs").contains("Node"));
   const auto& props = schema.at("$defs").at("Node").at("properties");
   REQUIRE(props.at("at").at("$ref").get<std::string>() == "#/$defs/Point");
   REQUIRE(props.at("id").at("maximum").as_uint64() == std::numeric_limits<std::uint64_t>::max());
}
TEST_CASE("5.17 - Optional field allows null but is required in Step-2 codec", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   auto schema = gen.type<Node>();
   const auto& node = schema.at("$defs").at("Node");
   REQUIRE(node.at("required").size() == 3);
   REQUIRE(node.at("properties").at("label").at("anyOf").size() == 2);
   REQUIRE_FALSE(node.at("additionalProperties").get<bool>());
}
TEST_CASE("5.18 - Recursive struct schema terminates and uses one definition", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   auto schema = gen.type<Recursive>();
   REQUIRE(schema.at("$defs").size() == 1);
   REQUIRE(
      schema.at("$defs").at("Recursive").at("properties").at("children").at("items").at("$ref").get<std::string>() ==
      "#/$defs/Recursive");
}
TEST_CASE("5.19 - Tool argument schema names and required params", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   auto schema = gen.method_arguments<Service>("get");
   REQUIRE(schema.at("properties").contains("id"));
   REQUIRE(schema.at("properties").contains("label"));
   REQUIRE(schema.at("required").size() == 2);
   REQUIRE(schema.at("description").get<std::string>() == "Get node");
   REQUIRE_FALSE(schema.at("additionalProperties").get<bool>());
}
TEST_CASE("5.20 - Method return schema emits registered Node", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   auto result = gen.method_result<Service>("get");
   REQUIRE(result.at("$ref").get<std::string>() == "#/$defs/Node");
   REQUIRE(result.at("$defs").contains("Point"));
   REQUIRE(gen.method_result<Service>("change").at("type").get<std::string>() == "null");
}
TEST_CASE("5.21 - Unsupported method parameter is rejected", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   schema_error([&] { (void)gen.method_arguments<Service>("reference"); }, SchemaErrorCode::UnsupportedParameter);
   schema_error([&] { (void)gen.method_arguments<Service>("unsupported"); }, SchemaErrorCode::UnsupportedParameter);
}
TEST_CASE("5.22 - Unsupported return is rejected", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   schema_error([&] { (void)gen.method_result<Service>("bad_result"); }, SchemaErrorCode::UnsupportedReturn);
}
TEST_CASE("5.23 - Unregistered nested field fails explicitly", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   schema_error([&] { (void)gen.type<Missing>(); }, SchemaErrorCode::UnregisteredType);
   schema_error([&] { (void)gen.type<int*>(); }, SchemaErrorCode::UnsupportedType);
}
TEST_CASE("5.24 - Overloaded method requires signature", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   REQUIRE_THROWS_AS(gen.method_arguments<Service>("overloaded"), ReflectionError);
   using Sig = int (Service::*)(int) const;
   auto schema = gen.method_arguments<Service>("overloaded", typeid(Sig));
   REQUIRE(schema.at("properties").contains("arg0"));
}
TEST_CASE("5.25 - JSON to invocation to typed result round trip", "[e2e]")
{
   auto reg = create_registry();
   Service service;
   const Value args = parse_json(R"({"id":18446744073709551615,"label":"test"})");
   const Value result = reg.invoke(service, "get", args);
   const auto serialized = to_json(result);
   const Node restored = reg.from_value<Node>(parse_json(serialized));
   REQUIRE(restored.id == std::numeric_limits<std::uint64_t>::max());
   REQUIRE(restored.at.x == 1.0);
   REQUIRE(restored.label.has_value());
   REQUIRE(*restored.label == "test");
}
TEST_CASE("5.26 - Method and type schemas are themselves JSON serializable", "[schema]")
{
   auto reg = create_registry();
   JsonSchemaGenerator gen(reg);
   auto a = gen.type<Node>();
   auto b = gen.method_arguments<Service>("get");
   REQUIRE(to_json(parse_json(to_json(a))) == to_json(a));
   REQUIRE(to_json(parse_json(to_json(b))) == to_json(b));
}
