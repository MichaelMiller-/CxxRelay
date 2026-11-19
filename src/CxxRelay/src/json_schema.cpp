#include <cxxrelay/json_schema.h>

#include <set>
#include <string>

namespace CxxRelay
{
   namespace
   {
      std::string pointer_token(std::string_view name)
      {
         std::string result;
         for (const char c : name) {
            if (c == '~')
               result += "~0";
            else if (c == '/')
               result += "~1";
            else
               result += c;
         }
         return result;
      }

      class Builder final
      {
      public:
         explicit Builder(const TypeRegistry& registry) : registry_(registry) {}

         Value schema(const TypeShape& shape)
         {
            using Kind = TypeShape::Kind;
            switch (shape.kind) {
            case Kind::Any:
               return Value::object();
            case Kind::Void:
            case Kind::Null:
               return Value::object({{"type", "null"}});
            case Kind::Boolean:
               return Value::object({{"type", "boolean"}});
            case Kind::String:
               return Value::object({{"type", "string"}});
            case Kind::Number:
               return Value::object({{"type", "number"}});
            case Kind::SignedInteger:
            case Kind::UnsignedInteger: {
               Value value = Value::object({{"type", "integer"}});
               if (shape.minimum)
                  value.set("minimum", *shape.minimum);
               if (shape.maximum)
                  value.set("maximum", *shape.maximum);
               return value;
            }
            case Kind::Array:
               return Value::object({{"type", "array"}, {"items", schema(*shape.element)}});
            case Kind::Optional:
               if (shape.element->kind == Kind::Any || shape.element->kind == Kind::Optional ||
                   shape.element->kind == Kind::Null || shape.element->kind == Kind::Void)
                  return schema(*shape.element);
               return Value::object(
                  {{"anyOf", Value::array({schema(*shape.element), Value::object({{"type", "null"}})})}});
            case Kind::Registered: {
               const auto* descriptor = registry_.find(shape.cpp_type);
               if (!descriptor)
                  throw SchemaError(SchemaErrorCode::UnregisteredType, "Referenced C++ type is not registered");
               define(*descriptor);
               return Value::object({{"$ref", "#/$defs/" + pointer_token(descriptor->name())}});
            }
            case Kind::Unsupported:
               throw SchemaError(SchemaErrorCode::UnsupportedType, "C++ type has no transportable JSON Schema");
            }
            throw SchemaError(SchemaErrorCode::UnsupportedType, "Unexpected TypeShape kind");
         }

         Value finish(Value root)
         {
            root.set("$schema", "https://json-schema.org/draft/2020-12/schema");
            if (!definitions_.get<Value::Object>().empty())
               root.set("$defs", std::move(definitions_));
            return root;
         }

      private:
         void define(const TypeDescriptor& descriptor)
         {
            const std::string name(descriptor.name());
            if (!defined_.insert(name).second)
               return; // Cycle-safe for vector<Self> etc.
            Value properties = Value::object();
            Value required = Value::array();
            for (const auto& field : descriptor.fields()) {
               properties.set(std::string(field.name()), schema(field.shape()));
               // Step-2 from_value requires all members, even optional<T>.
               required.push_back(std::string(field.name()));
            }
            Value schema_value = Value::object({{"type", "object"},
                                                {"properties", std::move(properties)},
                                                {"required", std::move(required)},
                                                {"additionalProperties", false}});
            definitions_.set(name, std::move(schema_value));
         }
         const TypeRegistry& registry_;
         std::set<std::string> defined_;
         Value definitions_ = Value::object();
      };

      void check_method_owner(const TypeRegistry& registry, const TypeDescriptor& owner, const MethodDescriptor& method)
      {
         const auto* registered = registry.find(owner.cpp_type());
         if (registered != &owner)
            throw SchemaError(SchemaErrorCode::UnregisteredType, "Method owner does not belong to this registry");
         const auto* matching = owner.find_method(method.name(), method.signature_type());
         if (matching != &method)
            throw ReflectionError(ReflectionErrorCode::UnknownMethod, "Method does not belong to its owner descriptor");
      }

   } // namespace

   Value JsonSchemaGenerator::type(const TypeDescriptor& descriptor) const
   {
      const auto* known = registry_.find(descriptor.cpp_type());
      if (known != &descriptor)
         throw SchemaError(SchemaErrorCode::UnregisteredType, "Type descriptor does not belong to this registry");
      TypeShape shape;
      shape.cpp_type = descriptor.cpp_type();
      shape.kind = TypeShape::Kind::Registered;
      return type_shape(shape);
   }

   Value JsonSchemaGenerator::type_shape(const TypeShape& shape) const
   {
      Builder builder(registry_);
      return builder.finish(builder.schema(shape));
   }

   Value JsonSchemaGenerator::arguments(const TypeDescriptor& owner, const MethodDescriptor& method) const
   {
      check_method_owner(registry_, owner, method);
      Builder builder(registry_);
      Value properties = Value::object();
      Value required = Value::array();
      for (const auto& parameter : method.parameters()) {
         if (parameter.passing() == PassingMode::LValueReference ||
             parameter.passing() == PassingMode::ConstRValueReference) {
            throw SchemaError(SchemaErrorCode::UnsupportedParameter,
                              "Parameter has an unsupported reference mode: " + std::string(parameter.name()));
         }
         try {
            properties.set(std::string(parameter.name()), builder.schema(parameter.shape()));
         } catch (const SchemaError& error) {
            throw SchemaError(SchemaErrorCode::UnsupportedParameter,
                              "Parameter '" + std::string(parameter.name()) + "': " + error.what());
         }
         required.push_back(std::string(parameter.name()));
      }
      Value root = Value::object({{"type", "object"},
                                  {"properties", std::move(properties)},
                                  {"required", std::move(required)},
                                  {"additionalProperties", false}});
      if (!method.description().empty())
         root.set("description", std::string(method.description()));
      return builder.finish(std::move(root));
   }

   Value JsonSchemaGenerator::result(const TypeDescriptor& owner, const MethodDescriptor& method) const
   {
      check_method_owner(registry_, owner, method);
      Builder builder(registry_);
      try {
         return builder.finish(builder.schema(method.return_shape()));
      } catch (const SchemaError& error) {
         throw SchemaError(SchemaErrorCode::UnsupportedReturn, "Return value: " + std::string(error.what()));
      }
   }
} // namespace CxxRelay
