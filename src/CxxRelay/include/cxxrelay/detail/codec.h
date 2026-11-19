#pragma once

// Included at the bottom of reflection.hpp, after TypeRegistry is complete.

#include <cmath>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace CxxRelay::detail
{
   template <class T>
   struct vector_traits : std::false_type
   {
   };
   template <class Item, class Allocator>
   struct vector_traits<std::vector<Item, Allocator>> : std::true_type
   {
      using item_type = Item;
   };

   template <class T>
   struct optional_traits : std::false_type
   {
   };
   template <class Item>
   struct optional_traits<std::optional<Item>> : std::true_type
   {
      using item_type = Item;
   };

   inline ReflectionError translate(const ValueError& error)
   {
      const auto code = (error.code() == ValueErrorCode::Overflow || error.code() == ValueErrorCode::OutOfRange)
                           ? ReflectionErrorCode::OutOfRange
                           : ReflectionErrorCode::TypeMismatch;
      return ReflectionError(code, error.what());
   }

   template <class T>
   Value encode(const T& value, const TypeRegistry& registry)
   {
      if constexpr (std::is_same_v<T, Value>) {
         return value;
      } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
         return Value(nullptr);
      } else if constexpr (std::is_same_v<T, bool>) {
         return Value(value);
      } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
         static_assert(sizeof(T) <= sizeof(std::uint64_t), "Integers larger than 64 bits require a custom codec");
         return Value(value);
      } else if constexpr (std::is_floating_point_v<T>) {
         static_assert(sizeof(T) <= sizeof(double), "Floating-point types wider than double require a custom codec");
         return Value(static_cast<double>(value));
      } else if constexpr (std::is_same_v<T, std::string>) {
         return Value(value);
      } else if constexpr (vector_traits<T>::value) {
         Value::Array elements;
         elements.reserve(value.size());
         for (const auto& element : value) {
            elements.push_back(registry.to_value(element));
         }
         return Value(std::move(elements));
      } else if constexpr (optional_traits<T>::value) {
         return value ? registry.to_value(*value) : Value(nullptr);
      } else if constexpr (std::is_class_v<T>) {
         return registry.registered_to_value(value);
      } else {
         static_assert(std::is_class_v<T>, "Unsupported field type; register a custom adapter later");
      }
   }

   template <class T>
   T decode(const Value& value, const TypeRegistry& registry)
   {
      if constexpr (std::is_same_v<T, Value>) {
         return value;
      } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
         if (!value.is_null()) {
            throw ReflectionError(ReflectionErrorCode::TypeMismatch, "Expected null value");
         }
         return nullptr;
      } else if constexpr (std::is_same_v<T, bool>) {
         if (!value.is<bool>()) {
            throw ReflectionError(ReflectionErrorCode::TypeMismatch, "Expected bool value");
         }
         return value.get<bool>();
      } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
         static_assert(sizeof(T) <= sizeof(std::uint64_t), "Integers larger than 64 bits require a custom codec");
         try {
            if constexpr (std::is_signed_v<T>) {
               const auto number = value.as_int64();
               if (!std::in_range<T>(number)) {
                  throw ReflectionError(ReflectionErrorCode::OutOfRange, "Signed integer is outside target type range");
               }
               return static_cast<T>(number);
            } else {
               const auto number = value.as_uint64();
               if (!std::in_range<T>(number)) {
                  throw ReflectionError(ReflectionErrorCode::OutOfRange,
                                        "Unsigned integer is outside target type range");
               }
               return static_cast<T>(number);
            }
         } catch (const ValueError& error) {
            throw translate(error);
         }
      } else if constexpr (std::is_floating_point_v<T>) {
         static_assert(sizeof(T) <= sizeof(double), "Floating-point types wider than double require a custom codec");
         try {
            // Numeric Value integers may be converted to double only if they
            // fit exactly; double->float is also constrained to be lossless.
            const double number = value.as_double();
            const T narrowed = static_cast<T>(number);
            if (!std::isfinite(narrowed) || static_cast<double>(narrowed) != number) {
               throw ReflectionError(ReflectionErrorCode::OutOfRange,
                                     "Floating-point value cannot be represented exactly");
            }
            return narrowed;
         } catch (const ValueError& error) {
            throw translate(error);
         }
      } else if constexpr (std::is_same_v<T, std::string>) {
         if (!value.is<std::string>()) {
            throw ReflectionError(ReflectionErrorCode::TypeMismatch, "Expected string value");
         }
         return value.get<std::string>();
      } else if constexpr (vector_traits<T>::value) {
         if (!value.is<Value::Array>()) {
            throw ReflectionError(ReflectionErrorCode::TypeMismatch, "Expected array value");
         }
         T result;
         const auto& items = value.get<Value::Array>();
         result.reserve(items.size());
         for (std::size_t i = 0; i < items.size(); ++i) {
            try {
               result.push_back(registry.from_value<typename vector_traits<T>::item_type>(items[i]));
            } catch (const ReflectionError& error) {
               throw ReflectionError(error.code(), "element[" + std::to_string(i) + "]: " + error.what());
            }
         }
         return result;
      } else if constexpr (optional_traits<T>::value) {
         if (value.is_null()) {
            return std::nullopt;
         }
         return registry.from_value<typename optional_traits<T>::item_type>(value);
      } else if constexpr (std::is_class_v<T>) {
         return registry.registered_from_value<T>(value);
      } else {
         static_assert(std::is_class_v<T>, "Unsupported field type; register a custom adapter later");
      }
   }

} // namespace CxxRelay::detail
