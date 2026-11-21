#pragma once

#include <cxxrelay/error.h>
#include <cxxrelay/field_descriptor.h>
#include <cxxrelay/method_descriptor.h>
#include <cxxrelay/value.h>

#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

namespace CxxRelay
{
   class TypeRegistry;

   template <typename T>
   class TypeBuilder;

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

      template <typename>
      friend class TypeBuilder;
   };
} // namespace CxxRelay