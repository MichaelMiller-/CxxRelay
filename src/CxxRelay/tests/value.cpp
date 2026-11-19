#include <catch2/catch_test_macros.hpp>

#include <cxxrelay/value.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

using CxxRelay::Value;
using CxxRelay::ValueError;
using CxxRelay::ValueErrorCode;

TEST_CASE("1.1 - Value defaults to null", "[value]")
{
   const Value value;
   REQUIRE(value.is_null());
   REQUIRE(value.kind() == Value::Kind::Null);
   REQUIRE(value.type_name() == "null");
   REQUIRE(Value(nullptr).is_null());
}

TEST_CASE("1.2 - Value stores primitive types with stable type identity", "[value]")
{
   const Value boolean = true;
   const Value signed_integer = -7;
   const Value unsigned_integer = std::uint64_t{42};
   const Value floating = 1.25;
   const Value text = "hello";

   REQUIRE(boolean.is<bool>());
   REQUIRE(boolean.get<bool>());
   REQUIRE(signed_integer.is<std::int64_t>());
   REQUIRE(signed_integer.get<std::int64_t>() == -7);
   REQUIRE(unsigned_integer.is<std::uint64_t>());
   REQUIRE(unsigned_integer.get<std::uint64_t>() == 42);
   REQUIRE(floating.is<double>());
   REQUIRE(floating.get<double>() == 1.25);
   REQUIRE(text.is<std::string>());
   REQUIRE(text.get<std::string>() == "hello");
   REQUIRE_FALSE(signed_integer == Value(std::uint64_t{0}));
   REQUIRE(Value(1) != Value(1u));
}

TEST_CASE("1.3 - Empty string, string view, and embedded zero are preserved", "[value]")
{
   REQUIRE(Value(std::string{}).get<std::string>().empty());
   const std::string_view original("a\0b", 3);
   const Value value(original);
   REQUIRE(value.get<std::string>().size() == 3);
   REQUIRE(value.get<std::string>()[1] == '\0');
}

TEST_CASE("1.4 - Invalid floating points and null C strings are rejected", "[value]")
{
   REQUIRE_THROWS_AS(Value(std::numeric_limits<double>::quiet_NaN()), ValueError);
   REQUIRE_THROWS_AS(Value(std::numeric_limits<double>::infinity()), ValueError);
   REQUIRE_THROWS_AS(Value(-std::numeric_limits<double>::infinity()), ValueError);
   const char* null_text = nullptr;
   REQUIRE_THROWS_AS(Value(null_text), ValueError);
}

TEST_CASE("1.5 - Exact typed access reports mismatch", "[value]")
{
   const Value value = "12";
   REQUIRE_FALSE(value.is<std::int64_t>());
   REQUIRE_THROWS_AS(value.get<std::int64_t>(), ValueError);
   REQUIRE_THROWS_AS(value.as_int64(), ValueError);
}

TEST_CASE("1.6 - Integer conversions preserve signedness and range", "[value]")
{
   REQUIRE(Value(std::uint64_t{42}).as_int64() == 42);
   REQUIRE(Value(std::int64_t{42}).as_uint64() == 42);
   REQUIRE_THROWS_AS(Value(-1).as_uint64(), ValueError);
   REQUIRE_THROWS_AS(Value(std::numeric_limits<std::uint64_t>::max()).as_int64(), ValueError);
   REQUIRE(Value(std::numeric_limits<std::uint64_t>::max()).get<std::uint64_t>() ==
           std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("1.7 - Double conversion rejects potentially lossy integers", "[value]")
{
   constexpr std::uint64_t exact_limit = std::uint64_t{1} << 53;
   REQUIRE(Value(exact_limit).as_double() == static_cast<double>(exact_limit));
   REQUIRE(Value(-static_cast<std::int64_t>(exact_limit)).as_double() == -static_cast<double>(exact_limit));
   REQUIRE_THROWS_AS(Value(exact_limit + 1).as_double(), ValueError);
   REQUIRE_THROWS_AS(Value(-static_cast<std::int64_t>(exact_limit) - 1).as_double(), ValueError);
   REQUIRE_THROWS_AS(Value("3.14").as_double(), ValueError);
   REQUIRE_THROWS_AS(Value(1.5).as_int64(), ValueError);
}

TEST_CASE("1.8 - Arrays are recursive and mutable through the Value API", "[value]")
{
   Value values = Value::array({1, "two", Value::array({true})});
   REQUIRE(values.kind() == Value::Kind::Array);
   REQUIRE(values.size() == 3);
   REQUIRE(values.at(std::size_t{2}).at(std::size_t{0}).get<bool>());
   values.push_back(nullptr);
   REQUIRE(values.size() == 4);
   REQUIRE(values.at(std::size_t{3}).is_null());
}

TEST_CASE("1.9 - Objects support nested lookup and replacement", "[value]")
{
   Value point = Value::object({{"id", std::uint64_t{123}}, {"position", Value::object({{"x", 1.0}, {"y", 2.0}})}});
   REQUIRE(point.size() == 2);
   REQUIRE(point.contains("position"));
   REQUIRE_FALSE(point.contains("missing"));
   REQUIRE(point.at("position").at("x").get<double>() == 1.0);
   point.at("position").set("x", 3.5);
   REQUIRE(point.at("position").at("x").get<double>() == 3.5);
}

TEST_CASE("1.10 - Missing keys, indexes, and wrong containers report errors", "[value]")
{
   const Value array = Value::array({1});
   const Value object = Value::object({{"one", 1}});
   REQUIRE_THROWS_AS(array.at(std::size_t{1}), ValueError);
   REQUIRE_THROWS_AS(object.at("two"), ValueError);
   REQUIRE_THROWS_AS(object.at(std::size_t{0}), ValueError);
   REQUIRE_THROWS_AS(array.at("one"), ValueError);
   REQUIRE_THROWS_AS(Value(5).size(), ValueError);
   REQUIRE_THROWS_AS(Value(5).contains("k"), ValueError);
}

TEST_CASE("1.11 - Duplicate keys are not silently dropped", "[value]")
{
   REQUIRE_THROWS_AS((Value::object({{"duplicate", 1}, {"duplicate", 2}})), ValueError);
}

TEST_CASE("1.12 - Assigning and copying composite values use value semantics", "[value]")
{
   Value original = Value::object({{"data", Value::array({1, 2})}});
   Value copy = original;
   copy.at("data").at(std::size_t{0}) = Value(99);
   REQUIRE(original.at("data").at(std::size_t{0}).as_int64() == 1);
   REQUIRE(copy.at("data").at(std::size_t{0}).as_int64() == 99);
   REQUIRE(original != copy);
}

TEST_CASE("1.13 - Move construction retains the contained value", "[value]")
{
   Value original = Value::array({"a", "b"});
   const Value moved = std::move(original);
   REQUIRE(moved.size() == 2);
   REQUIRE(moved.at(std::size_t{1}).get<std::string>() == "b");
}

TEST_CASE("1.14 - Mutation on the wrong kind cannot change the value", "[value]")
{
   Value object = Value::object();
   Value array = Value::array();
   REQUIRE_THROWS_AS(object.push_back(1), ValueError);
   REQUIRE_THROWS_AS(array.set("x", 1), ValueError);
   REQUIRE(object.size() == 0);
   REQUIRE(array.size() == 0);
}

TEST_CASE("1.15 - ValueError exposes a stable error code", "[value]")
{
   try {
      (void)Value(std::uint64_t{0}).at("x");
      FAIL("expected type mismatch");
   } catch (const ValueError& error) {
      REQUIRE(error.code() == ValueErrorCode::TypeMismatch);
   }
   try {
      (void)Value::array().at(std::size_t{0});
      FAIL("expected out of range");
   } catch (const ValueError& error) {
      REQUIRE(error.code() == ValueErrorCode::OutOfRange);
   }
}
