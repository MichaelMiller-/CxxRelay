#pragma once

#include <cxxrelay/error.h>
#include <cxxrelay/field_descriptor.h>
#include <cxxrelay/method_descriptor.h>
#include <cxxrelay/type_descriptor.h>
#include <cxxrelay/type_shape.h>
#include <cxxrelay/value.h>

#include <concepts>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace CxxRelay
{
   class TypeRegistry;

   template <typename T>
   class TypeBuilder;

   // Reflection errors are distinct from Value errors: they describe schema,
   // registration and C++ <-> Value conversion failures.
   enum class ReflectionErrorCode {
      DuplicateType,
      DuplicateTypeName,
      DuplicateField,
      UnregisteredType,
      TypeMismatch,
      OutOfRange,
      MissingField,
      UnknownField,
      DuplicateMethod,
      InvalidMethodName,
      InvalidMethodPointer,
      InvalidParameterNames,
      UnknownMethod,
      AmbiguousMethod,
      InvalidArguments,
      MissingArgument,
      UnknownArgument,
      ConstViolation,
      RefQualifierMismatch,
      UnsupportedParameter,
      UnsupportedReturn,
      InvocationFailed
   };

   using ReflectionError = Error<ReflectionErrorCode>;

   namespace detail
   {
      template <typename T>
      Value encode(const T&, const TypeRegistry&);
      template <typename T>
      T decode(const Value&, const TypeRegistry&);
   } // namespace detail

   class TypeRegistry final
   {
   public:
      TypeRegistry() = default;
      TypeRegistry(const TypeRegistry&) = delete;
      TypeRegistry& operator=(const TypeRegistry&) = delete;
      TypeRegistry(TypeRegistry&&) noexcept = default;
      TypeRegistry& operator=(TypeRegistry&&) noexcept = default;

      // A type is registered only once. The builder may then add its fields.
      // Repeated registrations (including under a different name) are errors.
      template <typename T>
      [[nodiscard]] TypeBuilder<T> type(std::string name);

      [[nodiscard]] const TypeDescriptor* find(std::string_view name) const noexcept;
      [[nodiscard]] const TypeDescriptor* find(std::type_index cpp_type) const noexcept;

      template <typename T>
      [[nodiscard]] const TypeDescriptor* find() const noexcept
      {
         const auto it = by_type_.find(std::type_index(typeid(T)));
         return it == by_type_.end() ? nullptr : it->second.get();
      }

      // Invoke one uniquely named registered method on a typed object.
      // Explicit signature selection is mandatory for overloaded reflection names.
      // `arguments` may be an ordered Value array or a named Value object.
      // Reflected methods cannot use mutable lvalue-reference parameters through
      // this transport API (no implicit copy-out semantics).
      template <typename Object>
      [[nodiscard]] Value invoke(Object&& object, std::string_view name, const Value& arguments = Value::array(),
                                 std::type_index signature = typeid(void)) const
      {
         using T = std::remove_cvref_t<Object>;
         static_assert(std::is_class_v<T>, "invoke() requires a class instance");
         const TypeDescriptor* descriptor = find<T>();
         if (!descriptor) {
            throw ReflectionError(ReflectionErrorCode::UnregisteredType,
                                  "Cannot invoke a method on an unregistered C++ type");
         }
         const void* readonly = std::addressof(object);
         void* writable = nullptr;
         if constexpr (!std::is_const_v<std::remove_reference_t<Object>>) {
            writable = std::addressof(object);
         }
         constexpr bool is_rvalue = !std::is_lvalue_reference_v<Object>;
         return invoke_erased(*descriptor, readonly, writable, is_rvalue, name, arguments, signature);
      }

      template <typename T>
      [[nodiscard]] Value to_value(const T& object) const
      {
         return detail::encode<std::remove_cvref_t<T>>(object, *this);
      }

      // Produces a new object. Existing objects are never partially modified
      // by a failed conversion. Registered classes must be default constructible.
      template <typename T>
      [[nodiscard]] T from_value(const Value& value) const
      {
         return detail::decode<T>(value, *this);
      }

   private:
      [[nodiscard]] Value invoke_erased(const TypeDescriptor&, const void* readonly, void* writable, bool is_rvalue,
                                        std::string_view name, const Value& arguments, std::type_index signature) const;

      template <typename T>
      [[nodiscard]] Value registered_to_value(const T& object) const
      {
         const auto* descriptor = find<T>();
         if (!descriptor) {
            throw ReflectionError(ReflectionErrorCode::UnregisteredType, "C++ type is not registered");
         }
         return descriptor->serialize(&object, *this);
      }

      template <typename T>
      [[nodiscard]] T registered_from_value(const Value& value) const
      {
         static_assert(std::default_initializable<T>,
                       "Deserializing a registered class requires a default constructor");
         const auto* descriptor = find<T>();
         if (!descriptor) {
            throw ReflectionError(ReflectionErrorCode::UnregisteredType, "C++ type is not registered");
         }
         T object{};
         descriptor->populate(&object, value, *this);
         return object;
      }

      template <typename T>
      friend Value detail::encode(const T&, const TypeRegistry&);
      template <typename T>
      friend T detail::decode(const Value&, const TypeRegistry&);

      std::unordered_map<std::type_index, std::unique_ptr<TypeDescriptor>> by_type_;
      std::map<std::string, TypeDescriptor*, std::less<>> by_name_;
   };

   template <typename T>
   class TypeBuilder final
   {
   public:
      // Member functions only. Overloaded C++ methods require an explicit cast
      // at the call site; similarly named reflected overloads are allowed if
      // their exact member-pointer signatures differ.
      template <typename MethodPointer>
      TypeBuilder& method(std::string name, MethodPointer pointer, MethodOptions options = {});

      template <typename Member>
      TypeBuilder& field(std::string name, Member T::* member)
      {
         static_assert(!std::is_const_v<Member>, "Read-only data members are not supported");
         static_assert(!std::is_pointer_v<Member> && !std::is_reference_v<Member>,
                       "Pointer/reference data members require an explicit future adapter");
         static_assert(std::is_assignable_v<Member&, Member>,
                       "Registered fields must be assignable during deserialization");

         descriptor_->add_field(
            FieldDescriptor{std::move(name), std::type_index(typeid(Member)), detail::shape_of<Member>(),
                            [member](const void* object, const TypeRegistry& registry) -> Value {
                               const auto& typed = *static_cast<const T*>(object);
                               return registry.to_value(typed.*member);
                            },
                            [member](void* object, const Value& value, const TypeRegistry& registry) {
                               auto& typed = *static_cast<T*>(object);
                               typed.*member = registry.from_value<Member>(value);
                            }});
         return *this;
      }

   private:
      explicit TypeBuilder(TypeDescriptor& descriptor) : descriptor_(&descriptor) {}

      TypeDescriptor* descriptor_;

      friend class TypeRegistry;
   };

   template <typename T>
   TypeBuilder<T> TypeRegistry::type(std::string name)
   {
      static_assert(std::is_class_v<T> && !std::is_union_v<T>,
                    "Register class/struct types; primitives use built-in codecs");
      const auto index = std::type_index(typeid(T));
      if (by_type_.contains(index)) {
         throw ReflectionError(ReflectionErrorCode::DuplicateType, "C++ type is already registered");
      }
      if (by_name_.contains(name)) {
         throw ReflectionError(ReflectionErrorCode::DuplicateTypeName,
                               "Reflection name is already registered: " + name);
      }
      if (name.empty()) {
         throw ReflectionError(ReflectionErrorCode::DuplicateTypeName, "Reflection type name must not be empty");
      }

      auto descriptor = std::unique_ptr<TypeDescriptor>(new TypeDescriptor(std::move(name), index));
      auto* ptr = descriptor.get();
      by_type_.emplace(index, std::move(descriptor));
      try {
         by_name_.emplace(std::string(ptr->name()), ptr);
      } catch (...) {
         by_type_.erase(index);
         throw;
      }
      return TypeBuilder<T>(*ptr);
   }
} // namespace CxxRelay

// Template codecs for primitives, containers and registered user types.
// The specializations are kept in a separate header to make dependencies
// and extension points explicit.
#include <cxxrelay/detail/codec.h>  // NOLINT
#include <cxxrelay/detail/method.h> // NOLINT
