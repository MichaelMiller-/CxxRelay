#pragma once

#include <cxxrelay/type_shape.h>

#include <functional>
#include <string_view>
#include <typeindex>

namespace CxxRelay
{
   class TypeRegistry;

   template <typename T>
   class TypeBuilder;

   class FieldDescriptor final
   {
   public:
      [[nodiscard]] std::string_view name() const noexcept { return name_; }
      [[nodiscard]] std::type_index cpp_type() const noexcept { return cpp_type_; }
      [[nodiscard]] const TypeShape& shape() const noexcept { return shape_; }

   private:
      using Reader = std::function<Value(const void*, const TypeRegistry&)>;
      using Writer = std::function<void(void*, const Value&, const TypeRegistry&)>;

      FieldDescriptor(std::string name, std::type_index cpp_type, TypeShape shape, Reader read, Writer write)
          : name_(std::move(name)), cpp_type_(cpp_type), shape_(std::move(shape)), read_(std::move(read)),
            write_(std::move(write))
      {
      }

      std::string name_;
      std::type_index cpp_type_;
      TypeShape shape_;
      Reader read_;
      Writer write_;

      friend class TypeDescriptor;

      template <class>
      friend class TypeBuilder;
   };
} // namespace CxxRelay