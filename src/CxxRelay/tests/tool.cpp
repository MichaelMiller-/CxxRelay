#include <catch2/catch_test_macros.hpp>

#include <cxxrelay/tool_dispatcher.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

using namespace CxxRelay;

namespace
{
   struct Point
   {
      int x{};
      int y{};
   };
   struct Service
   {
      std::string title = "draft";
      int updates = 0;
      std::string read(std::uint64_t id) const { return "doc-" + std::to_string(id); }
      void rename(std::string new_title)
      {
         title = std::move(new_title);
         ++updates;
      }
      int add_points(std::vector<Point> points, std::optional<int> bias) const
      {
         int sum = bias.value_or(0);
         for (const auto& p : points)
            sum += p.x + p.y;
         return sum;
      }
      std::string echo(std::string s) const { return s; }
      Value any(Value value) const { return value; }
      float fractional(float x) const { return x; }
      void throws_secret() const { throw std::runtime_error("private-file:/secret/data"); }
      void throws_reflection() const { throw ReflectionError(ReflectionErrorCode::UnknownMethod, "secret"); }
      int compute(int x) const { return x + 1; }
      std::string compute(std::string s) const { return s + "!"; }
      int dangerous_read()
      {
         ++updates;
         return 42;
      }
      int move_only() && { return 3; }
      int unsupported(int*) const { return 0; }
      void reference(int&) const {}
      int undocumented() const { return 6; }
      std::string invalid_utf8() const { return std::string(1, static_cast<char>(0x80)); }
   };

   using IntCompute = int (Service::*)(int) const;
   using StringCompute = std::string (Service::*)(std::string) const;

   TypeRegistry make_registry()
   {
      TypeRegistry r;
      r.type<Point>("Point").field("x", &Point::x).field("y", &Point::y);
      r.type<Service>("Service")
         .method("read", &Service::read, {.parameter_names = {"id"}, .description = "Read a document"})
         .method("rename", &Service::rename, {.parameter_names = {"new_title"}, .description = "Rename a document"})
         .method("add_points", &Service::add_points,
                 {.parameter_names = {"points", "bias"}, .description = "Add positions"})
         .method("echo", &Service::echo, {.parameter_names = {"s"}, .description = "Echo a string"})
         .method("any", &Service::any, {.parameter_names = {"value"}, .description = "Echo a Value"})
         .method("fractional", &Service::fractional, {.parameter_names = {"x"}, .description = "Float conversion"})
         .method("throws_secret", &Service::throws_secret, {.description = "Throw an application error"})
         .method("throws_reflection", &Service::throws_reflection, {.description = "Throw a reflection error"})
         .method("compute", static_cast<IntCompute>(&Service::compute),
                 {.parameter_names = {"x"}, .description = "Compute integer"})
         .method("compute", static_cast<StringCompute>(&Service::compute),
                 {.parameter_names = {"s"}, .description = "Compute string"})
         .method("dangerous_read", &Service::dangerous_read,
                 {.description = "Misclassified method", .access = MethodAccess::ReadOnly})
         .method("move_only", &Service::move_only, {.description = "Move service"})
         .method("unsupported", &Service::unsupported, {.parameter_names = {"p"}, .description = "Unsupported pointer"})
         .method("reference", &Service::reference, {.parameter_names = {"value"}, .description = "Unsupported ref"})
         .method("undocumented", &Service::undocumented)
         .method("invalid_utf8", &Service::invalid_utf8, {.description = "Return invalid UTF-8"});
      return r;
   }

   ToolContext reader() { return {.principal = "viewer", .allow_read = true}; }
   ToolContext writer() { return {.principal = "editor", .allow_read = true, .allow_write = true}; }

   ToolRegistrationErrorCode registration_error(const std::function<void()>& call)
   {
      try {
         call();
      } catch (const ToolRegistrationError& e) {
         return e.code();
      }
      FAIL("Expected ToolRegistrationError");
      return ToolRegistrationErrorCode::InvalidName;
   }

   struct Fixture
   {
      TypeRegistry registry = make_registry();
      std::shared_ptr<Service> service = std::make_shared<Service>();
      ToolDispatcher tools{registry};
      Fixture()
      {
         tools.expose("doc_read", service, "read")
            .expose("doc_rename", service, "rename")
            .expose("points_sum", service, "add_points")
            .expose("echo", service, "echo")
            .expose("any", service, "any")
            .expose("fractional", service, "fractional")
            .expose("throws_secret", service, "throws_secret")
            .expose("throws_reflection", service, "throws_reflection");
      }
   };
} // namespace

TEST_CASE("6.1 - Only explicitly exposed tools are visible", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.size() == 8);
   REQUIRE(f.tools.dispatch("dangerous_read", "{}", reader()).code == ToolErrorCode::UnknownTool);
}
TEST_CASE("6.2 - Definition schema is provider neutral", "[tool]")
{
   Fixture f;
   auto defs = f.tools.definitions(reader());
   REQUIRE(defs.is<Value::Array>());
   REQUIRE(defs.size() == 7);
   for (const auto& v : defs.get<Value::Array>()) {
      REQUIRE(v.at("parameters").at("type").get<std::string>() == "object");
      REQUIRE(v.at("access").get<std::string>() == "read_only");
      REQUIRE_FALSE(v.at("requires_confirmation").get<bool>());
   }
}
TEST_CASE("6.3 - Writer sees write tools with confirmation marker", "[tool]")
{
   Fixture f;
   const auto defs = f.tools.definitions(writer());
   REQUIRE(defs.size() == 8);
   bool found = false;
   for (const auto& v : defs.get<Value::Array>()) {
      if (v.at("name").get<std::string>() == "doc_rename") {
         found = true;
         REQUIRE(v.at("access").get<std::string>() == "write");
         REQUIRE(v.at("requires_confirmation").get<bool>());
      }
   }
   REQUIRE(found);
}
TEST_CASE("6.4 - Default deny hides all definitions", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.definitions({}).size() == 0);
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":1})", {}).code == ToolErrorCode::PermissionDenied);
}
TEST_CASE("6.5 - Successful read, typed result and JSON envelope", "[tool]")
{
   Fixture f;
   auto out = f.tools.dispatch("doc_read", R"({"id":42})", reader());
   REQUIRE(out.ok());
   REQUIRE(out.result.get<std::string>() == "doc-42");
   REQUIRE(parse_json(out.to_json()).at("result").get<std::string>() == "doc-42");
}
TEST_CASE("6.6 - Unsigned 64 bit arguments are exact", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":18446744073709551615})", reader()).result.get<std::string>() ==
           "doc-18446744073709551615");
}
TEST_CASE("6.7 - Invalid syntax and duplicate keys do not execute", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", "{bad", reader()).code == ToolErrorCode::InvalidArguments);
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":1,"id":2})", reader()).code == ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.8 - Tool arguments must be a named object", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", "[1]", reader()).code == ToolErrorCode::InvalidArguments);
   REQUIRE(f.tools.dispatch("doc_read", "null", reader()).code == ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.9 - Missing arguments are rejected", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", "{}", reader()).code == ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.10 - Extra arguments are rejected", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":2,"other":1})", reader()).code == ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.11 - Type errors are rejected", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":"42"})", reader()).code == ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.12 - Negative unsigned and integer-fraction are rejected", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":-1})", reader()).code == ToolErrorCode::InvalidArguments);
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":1.5})", reader()).code == ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.13 - Nested struct/array and optional conversion", "[tool]")
{
   Fixture f;
   const auto out = f.tools.dispatch("points_sum", R"({"points":[{"x":2,"y":3},{"x":4,"y":5}],"bias":6})", reader());
   REQUIRE(out.ok());
   REQUIRE(out.result.as_int64() == 20);
   REQUIRE(f.tools.dispatch("points_sum", R"({"points":[],"bias":null})", reader()).ok());
}
TEST_CASE("6.14 - Nested missing and unknown fields are rejected", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("points_sum", R"({"points":[{"x":1}],"bias":null})", reader()).code ==
           ToolErrorCode::InvalidArguments);
   REQUIRE(f.tools.dispatch("points_sum", R"({"points":[{"x":1,"y":2,"z":3}],"bias":null})", reader()).code ==
           ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.15 - Optional parameter still must be present", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("points_sum", R"({"points":[]})", reader()).code == ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.16 - Read-only user cannot mutate the service", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_rename", R"({"new_title":"HACK"})", reader()).code == ToolErrorCode::PermissionDenied);
   REQUIRE(f.service->title == "draft");
   REQUIRE(f.service->updates == 0);
}
TEST_CASE("6.17 - Writer cannot mutate without user confirmation", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_rename", R"({"new_title":"Edited"})", writer()).code ==
           ToolErrorCode::ConfirmationRequired);
   REQUIRE(f.service->updates == 0);
}
TEST_CASE("6.18 - Declined write leaves service untouched", "[tool]")
{
   Fixture f;
   auto context = writer();
   context.confirm_write = [](const ToolConfirmationRequest&) { return false; };
   REQUIRE(f.tools.dispatch("doc_rename", R"({"new_title":"Edited"})", context).code ==
           ToolErrorCode::ConfirmationDenied);
   REQUIRE(f.service->updates == 0);
}
TEST_CASE("6.19 - Trusted confirmation approves the exact call", "[tool]")
{
   Fixture f;
   auto context = writer();
   int requests = 0;
   context.confirm_write = [&](const ToolConfirmationRequest& req) {
      ++requests;
      REQUIRE(req.principal == "editor");
      REQUIRE(req.tool_name == "doc_rename");
      REQUIRE(req.arguments.at("new_title").get<std::string>() == "Approved");
      return true;
   };
   auto result = f.tools.dispatch("doc_rename", R"({"new_title":"Approved"})", context);
   REQUIRE(result.ok());
   REQUIRE(result.result.is_null());
   REQUIRE(requests == 1);
   REQUIRE(f.service->title == "Approved");
   REQUIRE(f.service->updates == 1);
}
TEST_CASE("6.20 - Approval is not requested for malformed arguments", "[tool]")
{
   Fixture f;
   auto context = writer();
   int calls = 0;
   context.confirm_write = [&](const ToolConfirmationRequest&) {
      ++calls;
      return true;
   };
   REQUIRE(f.tools.dispatch("doc_rename", R"({"new_title":42})", context).code == ToolErrorCode::InvalidArguments);
   REQUIRE(calls == 0);
   REQUIRE(f.service->updates == 0);
}
TEST_CASE("6.21 - Confirmation callback errors fail closed", "[tool]")
{
   Fixture f;
   auto context = writer();
   context.confirm_write = [](const ToolConfirmationRequest&) -> bool { throw std::runtime_error("bad gui"); };
   REQUIRE(f.tools.dispatch("doc_rename", R"({"new_title":"Changed"})", context).code ==
           ToolErrorCode::ConfirmationDenied);
   REQUIRE(f.service->updates == 0);
}
TEST_CASE("6.22 - Internal method exceptions are sanitized", "[tool]")
{
   Fixture f;
   auto result = f.tools.dispatch("throws_secret", "{}", reader());
   REQUIRE(result.code == ToolErrorCode::ExecutionFailed);
   REQUIRE(result.to_json().find("secret") == std::string::npos);
   result = f.tools.dispatch("throws_reflection", "{}", reader());
   REQUIRE(result.code == ToolErrorCode::ExecutionFailed);
   REQUIRE(result.to_json().find("secret") == std::string::npos);
}
TEST_CASE("6.23 - Exposing duplicate and invalid names fails", "[tool]")
{
   Fixture f;
   REQUIRE(registration_error([&] { f.tools.expose("doc_read", f.service, "read"); }) ==
           ToolRegistrationErrorCode::DuplicateName);
   REQUIRE(registration_error([&] { f.tools.expose("not allowed!", f.service, "read"); }) ==
           ToolRegistrationErrorCode::InvalidName);
   REQUIRE(registration_error([&] { f.tools.expose("1bad", f.service, "read"); }) ==
           ToolRegistrationErrorCode::InvalidName);
}
TEST_CASE("6.24 - Unknown, ambiguous and missing methods fail at exposure", "[tool]")
{
   Fixture f;
   REQUIRE(registration_error([&] { f.tools.expose("none", f.service, "missing"); }) ==
           ToolRegistrationErrorCode::UnknownMethod);
   REQUIRE(registration_error([&] { f.tools.expose("all_compute", f.service, "compute"); }) ==
           ToolRegistrationErrorCode::AmbiguousMethod);
}
TEST_CASE("6.25 - Exposing a specific overload works", "[tool]")
{
   Fixture f;
   f.tools.expose("int_compute", f.service, "compute", {}, typeid(IntCompute));
   f.tools.expose("string_compute", f.service, "compute", {}, typeid(StringCompute));
   REQUIRE(f.tools.dispatch("int_compute", R"({"x":41})", reader()).result.as_int64() == 42);
   REQUIRE(f.tools.dispatch("string_compute", R"({"s":"Hi"})", reader()).result.get<std::string>() == "Hi!");
}
TEST_CASE("6.26 - Unsupported signatures fail at exposure", "[tool]")
{
   Fixture f;
   REQUIRE(registration_error([&] { f.tools.expose("bad_ptr", f.service, "unsupported"); }) ==
           ToolRegistrationErrorCode::UnsafeMethod);
   REQUIRE(registration_error([&] { f.tools.expose("bad_ref", f.service, "reference"); }) ==
           ToolRegistrationErrorCode::UnsafeMethod);
   REQUIRE(registration_error([&] { f.tools.expose("bad_rvalue", f.service, "move_only"); }) ==
           ToolRegistrationErrorCode::UnsafeMethod);
   REQUIRE(registration_error([&] { f.tools.expose("bad_read", f.service, "dangerous_read"); }) ==
           ToolRegistrationErrorCode::UnsafeMethod);
}
TEST_CASE("6.27 - Const service forbids mutating method", "[tool]")
{
   auto registry = make_registry();
   ToolDispatcher tools(registry);
   std::shared_ptr<const Service> service = std::make_shared<const Service>();
   tools.expose("read", service, "read");
   REQUIRE(tools.dispatch("read", R"({"id":1})", reader()).ok());
   REQUIRE(registration_error([&] { tools.expose("write", service, "rename"); }) ==
           ToolRegistrationErrorCode::ConstViolation);
}
TEST_CASE("6.28 - Null service cannot be exposed", "[tool]")
{
   auto registry = make_registry();
   ToolDispatcher tools(registry);
   REQUIRE(registration_error([&] { tools.expose("read", std::shared_ptr<Service>{}, "read"); }) ==
           ToolRegistrationErrorCode::NullService);
}
TEST_CASE("6.29 - Service lifetime is owned by tool", "[tool]")
{
   auto registry = make_registry();
   ToolDispatcher tools(registry);
   auto ptr = std::make_shared<Service>();
   std::weak_ptr<Service> weak = ptr;
   tools.expose("read", ptr, "read");
   ptr.reset();
   REQUIRE_FALSE(weak.expired());
   REQUIRE(tools.dispatch("read", R"({"id":3})", reader()).ok());
}
TEST_CASE("6.30 - Audit hook sees outcome without arguments", "[tool]")
{
   Fixture f;
   int events = 0;
   f.tools.set_audit_sink([&](const ToolAuditEvent& event) {
      ++events;
      REQUIRE(event.principal == "viewer");
      REQUIRE(event.tool_name == "doc_read");
      REQUIRE((event.code == ToolErrorCode::None or event.code == ToolErrorCode::InvalidArguments));
   });
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":1})", reader()).ok());
   REQUIRE_FALSE(f.tools.dispatch("doc_read", R"({"id":"no"})", reader()).ok());
   REQUIRE(events == 2);
}
TEST_CASE("6.31 - Throwing audit sink cannot alter outcome", "[tool]")
{
   Fixture f;
   f.tools.set_audit_sink([](const ToolAuditEvent&) { throw std::runtime_error("sink unavailable"); });
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":4})", reader()).ok());
}
TEST_CASE("6.32 - JSON size limit on input rejects before execution", "[tool]")
{
   auto registry = make_registry();
   ToolDispatcher tools(registry, JsonOptions{.max_depth = 16, .max_bytes = 40, .max_nodes = 20});
   auto service = std::make_shared<Service>();
   tools.expose("rename", service, "rename");
   auto context = writer();
   context.confirm_write = [](const ToolConfirmationRequest&) { return true; };
   REQUIRE(
      tools.dispatch("rename", R"({"new_title":"A very long title with much more than forty characters"})", context)
         .code == ToolErrorCode::InvalidArguments);
   REQUIRE(service->updates == 0);
}
TEST_CASE("6.33 - JSON response size limit is enforced", "[tool]")
{
   auto registry = make_registry();
   ToolDispatcher tools(registry, JsonOptions{.max_depth = 16, .max_bytes = 70, .max_nodes = 200});
   auto service = std::make_shared<Service>();
   tools.expose("echo", service, "echo");
   const auto result =
      tools.dispatch("echo", R"({"s":"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"})", reader());
   REQUIRE(result.code == ToolErrorCode::ResponseLimit);
}
TEST_CASE("6.34 - Numeric float narrowing remains checked by codec", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("fractional", R"({"x":0.1})", reader()).code == ToolErrorCode::InvalidArguments);
   REQUIRE(f.tools.dispatch("fractional", R"({"x":0.5})", reader()).ok());
}
TEST_CASE("6.35 - Value may carry any JSON and preserves type", "[tool]")
{
   Fixture f;
   const auto r = f.tools.dispatch("any", R"({"value":{"items":[1,"a",null]}})", reader());
   REQUIRE(r.ok());
   REQUIRE(r.result.at("items").size() == 3);
}
TEST_CASE("6.36 - Unknown tool produces stable error envelope", "[tool]")
{
   Fixture f;
   auto output = parse_json(f.tools.dispatch("not_a_tool", "{}", reader()).to_json());
   REQUIRE_FALSE(output.at("ok").get<bool>());
   REQUIRE(output.at("error").at("code").get<std::string>() == "unknown_tool");
}

TEST_CASE("6.37 - Per-tool ACL hides unrelated tools", "[tool]")
{
   Fixture f;
   auto context = reader();
   context.allow_tool = [](std::string_view name, MethodAccess) { return name == "doc_read"; };
   REQUIRE(f.tools.definitions(context).size() == 1);
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":1})", context).ok());
   REQUIRE(f.tools.dispatch("echo", R"({"s":"hello"})", context).code == ToolErrorCode::PermissionDenied);
}
TEST_CASE("6.38 - Throwing authorization policy fails closed", "[tool]")
{
   Fixture f;
   auto context = reader();
   context.allow_tool = [](std::string_view, MethodAccess) -> bool {
      throw std::runtime_error("authorization service unavailable");
   };
   REQUIRE(f.tools.definitions(context).size() == 0);
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":1})", context).code == ToolErrorCode::PermissionDenied);
}
TEST_CASE("6.39 - Per-tool ACL cannot elevate a read-only role", "[tool]")
{
   Fixture f;
   auto context = reader();
   context.allow_tool = [](std::string_view, MethodAccess) { return true; };
   REQUIRE(f.tools.dispatch("doc_rename", R"({"new_title":"x"})", context).code == ToolErrorCode::PermissionDenied);
}
TEST_CASE("6.40 - Missing method description fails but override succeeds", "[tool]")
{
   Fixture f;
   REQUIRE(registration_error([&] { f.tools.expose("undocumented", f.service, "undocumented"); }) ==
           ToolRegistrationErrorCode::MissingDescription);
   f.tools.expose("document_info", f.service, "undocumented", {.description = "Display info"});
   REQUIRE(f.tools.dispatch("document_info", "{}", reader()).result.as_int64() == 6);
}
TEST_CASE("6.41 - Zero-byte and oversized argument limits are rejected", "[tool]")
{
   Fixture f;
   REQUIRE(f.tools.dispatch("doc_read", "", reader()).code == ToolErrorCode::InvalidArguments);
   REQUIRE(f.tools.dispatch("doc_read", R"({"id":9999999999999999999999999999999})", reader()).code ==
           ToolErrorCode::InvalidArguments);
}
TEST_CASE("6.42 - Unregistered service fails at exposure", "[tool]")
{
   struct LocalService
   {
   };
   TypeRegistry registry;
   ToolDispatcher tools(registry);
   REQUIRE(registration_error([&] { tools.expose("unknown", std::make_shared<LocalService>(), "read"); }) ==
           ToolRegistrationErrorCode::UnknownMethod);
}

TEST_CASE("6.43 - Unserializable application result becomes execution failure", "[tool]")
{
   Fixture f;
   f.tools.expose("bad_output", f.service, "invalid_utf8");
   auto output = f.tools.dispatch("bad_output", "{}", reader());
   REQUIRE(output.code == ToolErrorCode::ExecutionFailed);
   REQUIRE(output.to_json().find("invalid_utf8") == std::string::npos);
}
TEST_CASE("6.44 - Non-ASCII and oversized tool names are rejected", "[tool]")
{
   Fixture f;
   REQUIRE(registration_error([&] { f.tools.expose("äpfel", f.service, "read"); }) ==
           ToolRegistrationErrorCode::InvalidName);
   REQUIRE(registration_error([&] { f.tools.expose(std::string(65, 'a'), f.service, "read"); }) ==
           ToolRegistrationErrorCode::InvalidName);
}
