#include <cxxrelay/value.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace CxxRelay
{
   Value::Value(std::nullptr_t) noexcept : storage_(std::monostate{}) {}
   Value::Value(bool value) noexcept : storage_(value) {}

   Value::Value(double value) : storage_(value)
   {
      if (!std::isfinite(value)) {
         throw ValueError(ValueErrorCode::InvalidValue, "Value: non-finite floating-point numbers are not supported");
      }
   }

   Value::Value(std::string value) : storage_(std::move(value)) {}
   Value::Value(std::string_view value) : storage_(std::string(value)) {}

   Value::Value(const char* value)
   {
      if (value == nullptr) {
         throw ValueError(ValueErrorCode::InvalidValue, "Value: null C string pointer (use nullptr for null values)");
      }
      storage_ = std::string(value);
   }

   Value::Value(Array value) : storage_(std::move(value)) {}
   Value::Value(Object value) : storage_(std::move(value)) {}

   Value Value::array(std::initializer_list<Value> entries) { return Value(Array(entries)); }

   Value Value::object(std::initializer_list<std::pair<std::string, Value>> entries)
   {
      Object result;
      for (const auto& [key, value] : entries) {
         if (!result.emplace(key, value).second) {
            throw ValueError(ValueErrorCode::DuplicateKey, "Value: duplicate object key: " + key);
         }
      }
      return Value(std::move(result));
   }

   Value::Kind Value::kind() const noexcept { return static_cast<Kind>(storage_.index()); }

   std::string_view Value::type_name() const noexcept
   {
      constexpr std::array<std::string_view, 8> names{"null",   "bool",   "int64", "uint64",
                                                      "double", "string", "array", "object"};
      return names[storage_.index()];
   }

   bool Value::is_null() const noexcept { return std::holds_alternative<std::monostate>(storage_); }

   std::int64_t Value::as_int64() const
   {
      if (const auto* number = std::get_if<std::int64_t>(&storage_)) {
         return *number;
      }
      if (const auto* number = std::get_if<std::uint64_t>(&storage_)) {
         if (*number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw ValueError(ValueErrorCode::Overflow, "Value: uint64 cannot fit in int64");
         }
         return static_cast<std::int64_t>(*number);
      }
      throw ValueError(ValueErrorCode::TypeMismatch, "Value: as_int64 requires a signed or unsigned integer");
   }

   std::uint64_t Value::as_uint64() const
   {
      if (const auto* number = std::get_if<std::uint64_t>(&storage_)) {
         return *number;
      }
      if (const auto* number = std::get_if<std::int64_t>(&storage_)) {
         if (*number < 0) {
            throw ValueError(ValueErrorCode::Overflow, "Value: negative int64 cannot fit in uint64");
         }
         return static_cast<std::uint64_t>(*number);
      }
      throw ValueError(ValueErrorCode::TypeMismatch, "Value: as_uint64 requires a signed or unsigned integer");
   }

   double Value::as_double() const
   {
      if (const auto* number = std::get_if<double>(&storage_)) {
         return *number;
      }
      // Conservative safe limit: all integers in [-2^digits, +2^digits]
      // are exactly representable. Even though some larger integers also fit,
      // we reject them rather than perform a potentially lossy conversion.
      constexpr std::uint64_t max_exact = std::uint64_t{1} << std::numeric_limits<double>::digits;
      if (const auto* number = std::get_if<std::int64_t>(&storage_)) {
         if (*number < -static_cast<std::int64_t>(max_exact) || *number > static_cast<std::int64_t>(max_exact)) {
            throw ValueError(ValueErrorCode::Overflow, "Value: integer exceeds exact double conversion range");
         }
         return static_cast<double>(*number);
      }
      if (const auto* number = std::get_if<std::uint64_t>(&storage_)) {
         if (*number > max_exact) {
            throw ValueError(ValueErrorCode::Overflow, "Value: integer exceeds exact double conversion range");
         }
         return static_cast<double>(*number);
      }
      throw ValueError(ValueErrorCode::TypeMismatch, "Value: as_double requires a numeric value");
   }

   std::size_t Value::size() const
   {
      if (const auto* elements = std::get_if<Array>(&storage_)) {
         return elements->size();
      }
      if (const auto* members = std::get_if<Object>(&storage_)) {
         return members->size();
      }
      throw ValueError(ValueErrorCode::TypeMismatch, "Value: size requires an array or object");
   }

   bool Value::contains(std::string_view key) const { return get<Object>().find(key) != get<Object>().end(); }

   const Value& Value::at(std::size_t index) const
   {
      const auto& elements = get<Array>();
      if (index >= elements.size()) {
         throw ValueError(ValueErrorCode::OutOfRange, "Value: array index out of range");
      }
      return elements[index];
   }

   Value& Value::at(std::size_t index) { return const_cast<Value&>(std::as_const(*this).at(index)); }

   const Value& Value::at(std::string_view key) const
   {
      const auto& members = get<Object>();
      const auto it = members.find(key);
      if (it == members.end()) {
         throw ValueError(ValueErrorCode::OutOfRange, "Value: object key not found: " + std::string(key));
      }
      return it->second;
   }

   Value& Value::at(std::string_view key) { return const_cast<Value&>(std::as_const(*this).at(key)); }

   void Value::push_back(Value element)
   {
      if (auto* elements = std::get_if<Array>(&storage_)) {
         elements->push_back(std::move(element));
         return;
      }
      throw ValueError(ValueErrorCode::TypeMismatch, "Value: push_back requires an array");
   }

   void Value::set(std::string key, Value element)
   {
      if (auto* members = std::get_if<Object>(&storage_)) {
         members->insert_or_assign(std::move(key), std::move(element));
         return;
      }
      throw ValueError(ValueErrorCode::TypeMismatch, "Value: set requires an object");
   }

} // namespace CxxRelay::core
