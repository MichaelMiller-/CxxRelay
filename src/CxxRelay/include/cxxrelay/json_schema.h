#pragma once

#include <cxxrelay/core.h>
#include <cxxrelay/error.h>
#include <cxxrelay/type_shape.h>

#include <string>
#include <string_view>
#include <typeindex>

namespace CxxRelay
{
   enum class SchemaErrorCode { UnregisteredType, UnsupportedType, UnsupportedParameter, UnsupportedReturn };

   using SchemaError = Error<SchemaErrorCode>;

   // JSON Schema Draft 2020-12 generator. Outputs a Value, not a JSON string:
   // to_json(schema.type<T>()) is intentionally a separate adapter operation.
   class JsonSchemaGenerator final
   {
   public:
      explicit JsonSchemaGenerator(const TypeRegistry& registry) : registry_(registry) {}

      template <class T>
      [[nodiscard]] Value type() const
      {
         return type_shape(detail::shape_of<T>());
      }

      [[nodiscard]] Value type(const TypeDescriptor& descriptor) const;
      [[nodiscard]] Value type_shape(const TypeShape& shape) const;

      [[nodiscard]] Value arguments(const TypeDescriptor& owner, const MethodDescriptor& method) const;
      [[nodiscard]] Value result(const TypeDescriptor& owner, const MethodDescriptor& method) const;

      template <class T>
      [[nodiscard]] Value method_arguments(std::string_view name, std::type_index signature = typeid(void)) const
      {
         const auto& [owner, method] = resolve<T>(name, signature);
         return arguments(owner, method);
      }
      template <class T>
      [[nodiscard]] Value method_result(std::string_view name, std::type_index signature = typeid(void)) const
      {
         const auto& [owner, method] = resolve<T>(name, signature);
         return result(owner, method);
      }

   private:
      struct Resolved
      {
         const TypeDescriptor& owner;
         const MethodDescriptor& method;
      };

      template <class T>
      [[nodiscard]] Resolved resolve(std::string_view name, std::type_index signature) const
      {
         const auto* owner = registry_.find<T>();
         if (!owner)
            throw SchemaError(SchemaErrorCode::UnregisteredType, "Method owner is not registered");
         const MethodDescriptor* method = nullptr;
         if (signature == typeid(void)) {
            const auto matches = owner->find_methods(name);
            if (matches.size() > 1)
               throw ReflectionError(ReflectionErrorCode::AmbiguousMethod,
                                     "Method is overloaded; supply its signature");
            if (!matches.empty())
               method = matches.front();
         } else {
            method = owner->find_method(name, signature);
         }
         if (!method)
            throw ReflectionError(ReflectionErrorCode::UnknownMethod, "Method is not registered: " + std::string(name));
         return {*owner, *method};
      }
      const TypeRegistry& registry_;
   };
} // namespace CxxRelay
