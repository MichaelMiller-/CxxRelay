#pragma once

#include <cxxrelay/value.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace CxxRelay
{
   // A transport-independent description of the SHAPE of a C++ type.
   // It is intentionally separate from the JSON Schema adapter.
   struct TypeShape final
   {
      enum class Kind {
         Void,
         Any,
         Null,
         Boolean,
         SignedInteger,
         UnsignedInteger,
         Number,
         String,
         Array,
         Optional,
         Registered,
         Unsupported
      };

      Kind kind{Kind::Unsupported};
      std::type_index cpp_type{typeid(void)};
      std::shared_ptr<const TypeShape> element{};
      std::optional<Value> minimum{};
      std::optional<Value> maximum{};
   };

   namespace detail
   {
      template <class>
      struct shape_vector : std::false_type
      {
      };
      template <class T, class A>
      struct shape_vector<std::vector<T, A>> : std::true_type
      {
         using item = T;
      };
      template <class>
      struct shape_optional : std::false_type
      {
      };
      template <class T>
      struct shape_optional<std::optional<T>> : std::true_type
      {
         using item = T;
      };

      template <class T>
      TypeShape shape_of()
      {
         using U = std::remove_cvref_t<T>;
         TypeShape shape;
         shape.cpp_type = typeid(U);
         if constexpr (std::is_void_v<U>) {
            shape.kind = TypeShape::Kind::Void;
         } else if constexpr (std::is_same_v<U, Value>) {
            shape.kind = TypeShape::Kind::Any;
         } else if constexpr (std::is_same_v<U, std::nullptr_t>) {
            shape.kind = TypeShape::Kind::Null;
         } else if constexpr (std::is_same_v<U, bool>) {
            shape.kind = TypeShape::Kind::Boolean;
         } else if constexpr (std::is_integral_v<U> && std::is_signed_v<U> && sizeof(U) <= 8) {
            shape.kind = TypeShape::Kind::SignedInteger;
            shape.minimum = Value(std::numeric_limits<U>::min());
            shape.maximum = Value(std::numeric_limits<U>::max());
         } else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U> && sizeof(U) <= 8) {
            shape.kind = TypeShape::Kind::UnsignedInteger;
            shape.minimum = Value(std::uint64_t{0});
            shape.maximum = Value(std::numeric_limits<U>::max());
         } else if constexpr (std::is_floating_point_v<U> && sizeof(U) <= sizeof(double)) {
            shape.kind = TypeShape::Kind::Number;
         } else if constexpr (std::is_same_v<U, std::string>) {
            shape.kind = TypeShape::Kind::String;
         } else if constexpr (shape_vector<U>::value) {
            if constexpr (!std::is_same_v<U, std::vector<bool>>) {
               shape.kind = TypeShape::Kind::Array;
               shape.element = std::make_shared<TypeShape>(shape_of<typename shape_vector<U>::item>());
            }
         } else if constexpr (shape_optional<U>::value) {
            shape.kind = TypeShape::Kind::Optional;
            shape.element = std::make_shared<TypeShape>(shape_of<typename shape_optional<U>::item>());
         } else if constexpr (std::is_class_v<U> && !std::is_union_v<U>) {
            shape.kind = TypeShape::Kind::Registered;
         }
         return shape;
      }
   } // namespace detail
} // namespace CxxRelay
