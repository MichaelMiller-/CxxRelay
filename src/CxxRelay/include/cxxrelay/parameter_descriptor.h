#pragma once

#include <cxxrelay/passing_mode.h>
#include <cxxrelay/type_shape.h>

#include <string_view>
#include <typeindex>

namespace CxxRelay
{
   template <typename T>
   class TypeBuilder;

   class ParameterDescriptor final
   {
   public:
      [[nodiscard]] std::string_view name() const noexcept { return name_; }
      [[nodiscard]] std::type_index cpp_type() const noexcept { return cpp_type_; }
      [[nodiscard]] PassingMode passing() const noexcept { return passing_; }
      [[nodiscard]] const TypeShape& shape() const noexcept { return shape_; }

   private:
      ParameterDescriptor(std::string name, std::type_index cpp_type, PassingMode passing, TypeShape shape)
          : name_(std::move(name)), cpp_type_(cpp_type), passing_(passing), shape_(std::move(shape))
      {
      }

      std::string name_;
      std::type_index cpp_type_;
      PassingMode passing_;
      TypeShape shape_;

      template <class>
      friend class TypeBuilder;
   };
} // namespace CxxRelay