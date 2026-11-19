#pragma once

#include <cxxrelay/error.h>
#include <cxxrelay/field_descriptor.h>
#include <cxxrelay/parameter_descriptor.h>
#include <cxxrelay/passing_mode.h>
#include <cxxrelay/type_shape.h>
#include <cxxrelay/value.h>

#include <concepts>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

//! \todo most of the code will be replaced by C++26 reflection
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

   enum class RefQualifier { None, LValue, RValue };
   enum class MethodAccess { ReadOnly, Write };

   struct MethodOptions
   {
      std::vector<std::string> parameter_names{};
      std::string description{};
      // When absent, const methods default to ReadOnly and mutable methods
      // conservatively default to Write. Access is descriptive metadata only.
      std::optional<MethodAccess> access{};
   };

   class MethodDescriptor final
   {
   public:
      [[nodiscard]] std::string_view name() const noexcept { return name_; }
      [[nodiscard]] std::string_view description() const noexcept { return description_; }
      [[nodiscard]] MethodAccess access() const noexcept { return access_; }
      [[nodiscard]] std::type_index return_cpp_type() const noexcept { return return_type_; }
      [[nodiscard]] PassingMode return_passing() const noexcept { return return_passing_; }
      [[nodiscard]] const TypeShape& return_shape() const noexcept { return return_shape_; }
      [[nodiscard]] bool returns_void() const noexcept { return return_type_ == typeid(void); }
      [[nodiscard]] bool is_const() const noexcept { return is_const_; }
      [[nodiscard]] bool is_noexcept() const noexcept { return is_noexcept_; }
      [[nodiscard]] RefQualifier ref_qualifier() const noexcept { return ref_qualifier_; }
      // Identifies the EXACT member-function-pointer type (not a persistent ID).
      [[nodiscard]] std::type_index signature_type() const noexcept { return signature_type_; }
      [[nodiscard]] std::span<const ParameterDescriptor> parameters() const noexcept { return parameters_; }
      [[nodiscard]] const ParameterDescriptor* find_parameter(std::string_view name) const noexcept;

   private:
      // An erased adapter retains the original, type-safe member-function pointer.
      // The two object pointers refer to the SAME instance. The writable pointer
      // is null when the caller has supplied a const instance.
      using Invoker = std::function<Value(const void*, void*, bool, std::span<const Value>, const TypeRegistry&)>;

      MethodDescriptor(std::string name, std::string description, MethodAccess access, std::type_index return_type,
                       PassingMode return_passing, TypeShape return_shape, bool is_const, bool is_noexcept,
                       RefQualifier ref_qualifier, std::type_index signature_type,
                       std::vector<ParameterDescriptor> parameters, Invoker invoker)
          : name_(std::move(name)), description_(std::move(description)), access_(access), return_type_(return_type),
            return_passing_(return_passing), return_shape_(std::move(return_shape)), is_const_(is_const),
            is_noexcept_(is_noexcept), ref_qualifier_(ref_qualifier), signature_type_(signature_type),
            parameters_(std::move(parameters)), invoker_(std::move(invoker))
      {
      }

      std::string name_;
      std::string description_;
      MethodAccess access_;
      std::type_index return_type_;
      PassingMode return_passing_;
      TypeShape return_shape_;
      bool is_const_;
      bool is_noexcept_;
      RefQualifier ref_qualifier_;
      std::type_index signature_type_;
      std::vector<ParameterDescriptor> parameters_;
      Invoker invoker_;

      friend class TypeRegistry;
      friend class TypeDescriptor;
      template <class>
      friend class TypeBuilder;
   };

   class TypeDescriptor final
   {
   public:
      [[nodiscard]] std::string_view name() const noexcept { return name_; }
      [[nodiscard]] std::type_index cpp_type() const noexcept { return cpp_type_; }
      [[nodiscard]] std::span<const FieldDescriptor> fields() const noexcept { return fields_; }
      [[nodiscard]] const FieldDescriptor* find_field(std::string_view name) const noexcept;
      [[nodiscard]] std::span<const MethodDescriptor> methods() const noexcept { return methods_; }
      // Returns nullptr if no method matches OR the name is overloaded.
      [[nodiscard]] const MethodDescriptor* find_method(std::string_view name) const noexcept;
      [[nodiscard]] const MethodDescriptor* find_method(std::string_view name,
                                                        std::type_index signature_type) const noexcept;
      [[nodiscard]] std::vector<const MethodDescriptor*> find_methods(std::string_view name) const;

   private:
      TypeDescriptor(std::string name, std::type_index cpp_type) : name_(std::move(name)), cpp_type_(cpp_type) {}

      void add_field(FieldDescriptor field);
      void add_method(MethodDescriptor method);
      [[nodiscard]] Value serialize(const void* object, const TypeRegistry&) const;
      void populate(void* object, const Value&, const TypeRegistry&) const;

      std::string name_;
      std::type_index cpp_type_;
      std::vector<FieldDescriptor> fields_;
      std::vector<MethodDescriptor> methods_;

      friend class TypeRegistry;
      template <class>
      friend class TypeBuilder;
   };

   namespace detail
   {
      template <class T>
      Value encode(const T&, const TypeRegistry&);
      template <class T>
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
      template <class T>
      [[nodiscard]] TypeBuilder<T> type(std::string name);

      [[nodiscard]] const TypeDescriptor* find(std::string_view name) const noexcept;
      [[nodiscard]] const TypeDescriptor* find(std::type_index cpp_type) const noexcept;

      template <class T>
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
      template <class Object>
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

      template <class T>
      [[nodiscard]] Value to_value(const T& object) const
      {
         return detail::encode<std::remove_cvref_t<T>>(object, *this);
      }

      // Produces a new object. Existing objects are never partially modified
      // by a failed conversion. Registered classes must be default constructible.
      template <class T>
      [[nodiscard]] T from_value(const Value& value) const
      {
         return detail::decode<T>(value, *this);
      }

   private:
      [[nodiscard]] Value invoke_erased(const TypeDescriptor&, const void* readonly, void* writable, bool is_rvalue,
                                        std::string_view name, const Value& arguments, std::type_index signature) const;

      template <class T>
      [[nodiscard]] Value registered_to_value(const T& object) const
      {
         const auto* descriptor = find<T>();
         if (!descriptor) {
            throw ReflectionError(ReflectionErrorCode::UnregisteredType, "C++ type is not registered");
         }
         return descriptor->serialize(&object, *this);
      }

      template <class T>
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

      template <class T>
      friend Value detail::encode(const T&, const TypeRegistry&);
      template <class T>
      friend T detail::decode(const Value&, const TypeRegistry&);

      std::unordered_map<std::type_index, std::unique_ptr<TypeDescriptor>> by_type_;
      std::map<std::string, TypeDescriptor*, std::less<>> by_name_;
   };

   template <class T>
   class TypeBuilder final
   {
   public:
      // Member functions only. Overloaded C++ methods require an explicit cast
      // at the call site; similarly named reflected overloads are allowed if
      // their exact member-pointer signatures differ.
      template <class MethodPointer>
      TypeBuilder& method(std::string name, MethodPointer pointer, MethodOptions options = {});

      template <class Member>
      TypeBuilder& field(std::string name, Member T::* member)
      {
         static_assert(!std::is_const_v<Member>, "Read-only data members are not supported in Step 2");
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

   template <class T>
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
