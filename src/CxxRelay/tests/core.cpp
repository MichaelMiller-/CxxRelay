#include <catch2/catch_test_macros.hpp>

#include <cxxrelay/core.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

using namespace CxxRelay;

namespace
{
   struct Point3D
   {
      double x{};
      double y{};
      double z{};
      friend bool operator==(const Point3D&, const Point3D&) = default;
   };

   struct Node
   {
      std::uint64_t id{};
      Point3D position{};
      std::vector<std::string> tags{};
      std::optional<std::string> note{};
      friend bool operator==(const Node&, const Node&) = default;
   };

   struct Mesh
   {
      std::vector<Node> nodes{};
      std::optional<Point3D> origin{};
      friend bool operator==(const Mesh&, const Mesh&) = default;
   };

   struct Narrow
   {
      std::int8_t small{};
      std::uint16_t positive{};
      float weight{};
      friend bool operator==(const Narrow&, const Narrow&) = default;
   };

   struct Unregistered
   {
      int value{};
   };

   void register_geometry(TypeRegistry& registry)
   {
      registry.type<Point3D>("Point3D").field("x", &Point3D::x).field("y", &Point3D::y).field("z", &Point3D::z);
      registry.type<Node>("Node")
         .field("id", &Node::id)
         .field("position", &Node::position)
         .field("tags", &Node::tags)
         .field("note", &Node::note);
   }

   void expect_error_code(const std::function<void()>& operation, ReflectionErrorCode expected)
   {
      bool caught = false;
      try {
         operation();
      } catch (const ReflectionError& error) {
         caught = true;
         REQUIRE(error.code() == expected);
      }
      REQUIRE(caught);
   }

} // namespace

TEST_CASE("2.1 - Registry preserves type identity, names, and fields", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   const TypeDescriptor* type = registry.find<Point3D>();
   REQUIRE(type != nullptr);
   REQUIRE(type == registry.find("Point3D"));
   REQUIRE(type->name() == "Point3D");
   REQUIRE(type->cpp_type() == std::type_index(typeid(Point3D)));
   REQUIRE(type->fields().size() == 3);
   REQUIRE(type->fields()[0].name() == "x");
   REQUIRE(type->fields()[1].name() == "y");
   REQUIRE(type->fields()[2].name() == "z");
   REQUIRE(type->find_field("y") != nullptr);
   REQUIRE(type->find_field("y")->cpp_type() == std::type_index(typeid(double)));
   REQUIRE(type->find_field("missing") == nullptr);
   REQUIRE(registry.find("Unknown") == nullptr);
   REQUIRE(registry.find<Unregistered>() == nullptr);
}

TEST_CASE("2.2 - Duplicate type registrations are rejected", "[reflection]")
{
   TypeRegistry registry;
   registry.type<Point3D>("Point3D").field("x", &Point3D::x);
   expect_error_code([&] { (void)registry.type<Point3D>("SomeOtherName"); }, ReflectionErrorCode::DuplicateType);
   expect_error_code([&] { (void)registry.type<Node>("Point3D"); }, ReflectionErrorCode::DuplicateTypeName);
   REQUIRE(registry.find<Point3D>()->fields().size() == 1);
   REQUIRE(registry.find<Node>() == nullptr);
}

TEST_CASE("2.3 - Duplicate and empty field names are rejected", "[reflection]")
{
   TypeRegistry registry;
   auto builder = registry.type<Point3D>("Point3D");
   builder.field("x", &Point3D::x);
   expect_error_code([&] { builder.field("x", &Point3D::y); }, ReflectionErrorCode::DuplicateField);
   expect_error_code([&] { builder.field("", &Point3D::z); }, ReflectionErrorCode::DuplicateField);
   REQUIRE(registry.find<Point3D>()->fields().size() == 1);
}

TEST_CASE("2.4 - Primitive C++ types do not require registration", "[reflection]")
{
   TypeRegistry registry;
   REQUIRE(registry.to_value(true).get<bool>());
   REQUIRE(registry.to_value(-13).get<std::int64_t>() == -13);
   REQUIRE(registry.to_value(std::uint64_t{99}).as_uint64() == 99);
   REQUIRE(registry.to_value(2.25).get<double>() == 2.25);
   REQUIRE(registry.to_value(std::string("hello")).get<std::string>() == "hello");
   REQUIRE(registry.from_value<int>(Value(13)) == 13);
   REQUIRE(registry.from_value<std::uint32_t>(Value(99u)) == 99);
   REQUIRE(registry.from_value<double>(Value(2.25)) == 2.25);
   REQUIRE(registry.from_value<std::string>(Value("hi")) == "hi");
   REQUIRE(registry.from_value<Value>(Value::array({1})).size() == 1);
}

TEST_CASE("2.5 - Registered struct is serialized to a Value object", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   const Point3D original{1.0, -2.5, 3.75};
   const Value actual = registry.to_value(original);
   const Value expected = Value::object({{"x", 1.0}, {"y", -2.5}, {"z", 3.75}});
   REQUIRE(actual == expected);
   REQUIRE(registry.from_value<Point3D>(actual) == original);
}

TEST_CASE("2.6 - Nested structs, string lists and empty optional fields roundtrip", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   const Node original{
      std::numeric_limits<std::uint64_t>::max(), {1.0, 2.0, 3.0}, {"selected", "active"}, std::nullopt};
   const Value value = registry.to_value(original);
   REQUIRE(value.at("id").get<std::uint64_t>() == std::numeric_limits<std::uint64_t>::max());
   REQUIRE(value.at("position").at("z").get<double>() == 3.0);
   REQUIRE(value.at("tags").size() == 2);
   REQUIRE(value.at("note").is_null());
   REQUIRE(registry.from_value<Node>(value) == original);
}

TEST_CASE("2.7 - Optional values encode as content or null", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   Node original{7, {}, {}, std::string("hello")};
   const Value encoded = registry.to_value(original);
   REQUIRE(encoded.at("note").get<std::string>() == "hello");
   REQUIRE(registry.from_value<Node>(encoded) == original);
   REQUIRE_FALSE(registry.from_value<std::optional<int>>(Value(nullptr)).has_value());
   REQUIRE(registry.from_value<std::optional<int>>(Value(17)).value() == 17);
}

TEST_CASE("2.8 - Vector of registered types and optional struct are recursive", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   registry.type<Mesh>("Mesh").field("nodes", &Mesh::nodes).field("origin", &Mesh::origin);
   const Mesh source{{Node{1, {1, 2, 3}, {"A"}, std::nullopt}, Node{2, {4, 5, 6}, {"B"}, "test"}}, Point3D{0, 0, 0}};
   const Value encoded = registry.to_value(source);
   REQUIRE(encoded.at("nodes").at(std::size_t{1}).at("position").at("y").as_double() == 5);
   REQUIRE(encoded.at("origin").at("x").as_double() == 0);
   REQUIRE(registry.from_value<Mesh>(encoded) == source);
}

TEST_CASE("2.9 - Missing and unexpected fields are explicitly rejected", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   const Value missing = Value::object({{"x", 1.0}, {"y", 2.0}});
   const Value extra = Value::object({{"x", 1.0}, {"y", 2.0}, {"z", 3.0}, {"private", 4.0}});
   expect_error_code([&] { (void)registry.from_value<Point3D>(missing); }, ReflectionErrorCode::MissingField);
   expect_error_code([&] { (void)registry.from_value<Point3D>(extra); }, ReflectionErrorCode::UnknownField);
}

TEST_CASE("2.10 - Wrong primitive and object types are rejected", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   const Value wrong = Value::object({{"x", "not a number"}, {"y", 2.0}, {"z", 3.0}});
   expect_error_code([&] { (void)registry.from_value<Point3D>(Value::array()); }, ReflectionErrorCode::TypeMismatch);
   expect_error_code([&] { (void)registry.from_value<Point3D>(wrong); }, ReflectionErrorCode::TypeMismatch);
   expect_error_code([&] { (void)registry.from_value<bool>(Value(1)); }, ReflectionErrorCode::TypeMismatch);
}

TEST_CASE("2.11 - Narrow integer conversions reject overflow and sign errors", "[reflection]")
{
   TypeRegistry registry;
   registry.type<Narrow>("Narrow")
      .field("small", &Narrow::small)
      .field("positive", &Narrow::positive)
      .field("weight", &Narrow::weight);
   const Narrow initial{-12, 65535, 0.5f};
   REQUIRE(registry.from_value<Narrow>(registry.to_value(initial)) == initial);

   expect_error_code([&] { (void)registry.from_value<std::int8_t>(Value(128)); }, ReflectionErrorCode::OutOfRange);
   expect_error_code([&] { (void)registry.from_value<std::uint16_t>(Value(-1)); }, ReflectionErrorCode::OutOfRange);
   expect_error_code([&] { (void)registry.from_value<std::uint16_t>(Value(65536u)); }, ReflectionErrorCode::OutOfRange);
   expect_error_code([&] { (void)registry.from_value<std::int64_t>(Value(std::numeric_limits<std::uint64_t>::max())); },
                     ReflectionErrorCode::OutOfRange);
}

TEST_CASE("2.12 - Double-to-float conversion cannot silently lose precision", "[reflection]")
{
   TypeRegistry registry;
   REQUIRE(registry.from_value<float>(Value(0.5)) == 0.5f);
   expect_error_code([&] { (void)registry.from_value<float>(Value(0.1)); }, ReflectionErrorCode::OutOfRange);
   expect_error_code([&] { (void)registry.from_value<double>(Value(std::uint64_t{(1ULL << 53) + 1})); },
                     ReflectionErrorCode::OutOfRange);
}

TEST_CASE("2.13 - Unregistered structs are rejected in both directions", "[reflection]")
{
   TypeRegistry registry;
   expect_error_code([&] { (void)registry.to_value(Unregistered{42}); }, ReflectionErrorCode::UnregisteredType);
   expect_error_code([&] { (void)registry.from_value<Unregistered>(Value::object()); },
                     ReflectionErrorCode::UnregisteredType);
}

TEST_CASE("2.14 - Nested unregistered types include field context", "[reflection]")
{
   TypeRegistry registry;
   registry.type<Node>("Node").field("position", &Node::position);
   const Node node{};
   bool caught = false;
   try {
      (void)registry.to_value(node);
   } catch (const ReflectionError& error) {
      caught = true;
      REQUIRE(error.code() == ReflectionErrorCode::UnregisteredType);
      REQUIRE(std::string(error.what()).find("Node.position") != std::string::npos);
   }
   REQUIRE(caught);
}

TEST_CASE("2.15 - Type names do not affect C++ type identity", "[reflection]")
{
   TypeRegistry first;
   TypeRegistry second;
   first.type<Point3D>("Point3D").field("x", &Point3D::x);
   second.type<Point3D>("Coordinate").field("x", &Point3D::x);
   REQUIRE(first.find("Coordinate") == nullptr);
   REQUIRE(second.find("Point3D") == nullptr);
   REQUIRE(first.find<Point3D>()->cpp_type() == second.find<Point3D>()->cpp_type());
}

TEST_CASE("2.16 - TypeDescriptor addresses stay stable on subsequent registration", "[reflection]")
{
   TypeRegistry registry;
   const TypeDescriptor* pointer = nullptr;
   {
      auto builder = registry.type<Point3D>("Point3D");
      builder.field("x", &Point3D::x);
      pointer = registry.find<Point3D>();
   }
   registry.type<Node>("Node").field("id", &Node::id);
   REQUIRE(pointer != nullptr);
   REQUIRE(pointer == registry.find("Point3D"));
   REQUIRE(pointer->fields().size() == 1);
}

TEST_CASE("2.17 - Modifying converted values cannot change original objects", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   const Node node{3, {1, 2, 3}, {"original"}, std::nullopt};
   Value encoded = registry.to_value(node);
   encoded.at("position").set("x", 100.0);
   encoded.at("tags").push_back("new");
   const Node changed = registry.from_value<Node>(encoded);
   REQUIRE(node.position.x == 1.0);
   REQUIRE(node.tags.size() == 1);
   REQUIRE(changed.position.x == 100.0);
   REQUIRE(changed.tags.size() == 2);
}

TEST_CASE("2.18 - Indexed error context identifies invalid list elements", "[reflection]")
{
   TypeRegistry registry;
   register_geometry(registry);
   Value encoded = registry.to_value(Node{1, {1, 2, 3}, {"a"}, std::nullopt});
   encoded.at("tags").push_back(99);
   bool caught = false;
   try {
      (void)registry.from_value<Node>(encoded);
   } catch (const ReflectionError& error) {
      caught = true;
      REQUIRE(error.code() == ReflectionErrorCode::TypeMismatch);
      REQUIRE(std::string(error.what()).find("Node.tags: element[1]") != std::string::npos);
   }
   REQUIRE(caught);
}

TEST_CASE("2.19 - Vector of booleans is supported", "[reflection]")
{
   TypeRegistry registry;
   const std::vector<bool> bits{true, false, true};
   const Value encoded = registry.to_value(bits);
   REQUIRE(encoded.at(std::size_t{1}).get<bool>() == false);
   REQUIRE(registry.from_value<std::vector<bool>>(encoded) == bits);
}

TEST_CASE("2.20 - Attempting to deserialize a bool from an integer is not implicit", "[reflection]")
{
   TypeRegistry registry;
   expect_error_code([&] { (void)registry.from_value<bool>(Value(0)); }, ReflectionErrorCode::TypeMismatch);
   expect_error_code([&] { (void)registry.from_value<std::string>(Value(8)); }, ReflectionErrorCode::TypeMismatch);
}
