#pragma once

#include <cxxrelay/method_options.h>
#include <cxxrelay/parameter_descriptor.h>
#include <cxxrelay/passing_mode.h>
#include <cxxrelay/ref_qualifier.h>
#include <cxxrelay/type_shape.h>
#include <cxxrelay/value.h>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CxxRelay
{
   class TypeRegistry;

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
} // namespace CxxRelay