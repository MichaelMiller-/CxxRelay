#include <cxxrelay/core.h>

namespace CxxRelay
{
   const FieldDescriptor* TypeDescriptor::find_field(std::string_view name) const noexcept
   {
      for (const auto& field : fields_) {
         if (field.name() == name) {
            return &field;
         }
      }
      return nullptr;
   }

   const ParameterDescriptor* MethodDescriptor::find_parameter(std::string_view name) const noexcept
   {
      for (const auto& parameter : parameters_) {
         if (parameter.name() == name) {
            return &parameter;
         }
      }
      return nullptr;
   }

   const MethodDescriptor* TypeDescriptor::find_method(std::string_view name) const noexcept
   {
      const MethodDescriptor* match = nullptr;
      for (const auto& method : methods_) {
         if (method.name() == name) {
            if (match) {
               return nullptr; // Ambiguous: never silently choose an overload.
            }
            match = &method;
         }
      }
      return match;
   }

   const MethodDescriptor* TypeDescriptor::find_method(std::string_view name,
                                                       std::type_index signature_type) const noexcept
   {
      for (const auto& method : methods_) {
         if (method.name() == name && method.signature_type() == signature_type) {
            return &method;
         }
      }
      return nullptr;
   }

   std::vector<const MethodDescriptor*> TypeDescriptor::find_methods(std::string_view name) const
   {
      std::vector<const MethodDescriptor*> result;
      for (const auto& method : methods_) {
         if (method.name() == name) {
            result.push_back(&method);
         }
      }
      return result;
   }

   void TypeDescriptor::add_method(MethodDescriptor method)
   {
      if (method.name().empty()) {
         throw ReflectionError(ReflectionErrorCode::InvalidMethodName,
                               "Method name in '" + name_ + "' must not be empty");
      }
      if (find_method(method.name(), method.signature_type()) != nullptr) {
         throw ReflectionError(ReflectionErrorCode::DuplicateMethod,
                               "Duplicate method name/signature in '" + name_ + "': " + std::string(method.name()));
      }
      methods_.push_back(std::move(method));
   }

   void TypeDescriptor::add_field(FieldDescriptor field)
   {
      if (field.name().empty() || find_field(field.name()) != nullptr) {
         throw ReflectionError(ReflectionErrorCode::DuplicateField,
                               "Empty or duplicate field name in '" + name_ + "': " + std::string(field.name()));
      }
      fields_.push_back(std::move(field));
   }

   Value TypeDescriptor::serialize(const void* object, const TypeRegistry& registry) const
   {
      Value result = Value::object();
      for (const auto& field : fields_) {
         try {
            result.set(std::string(field.name()), field.read_(object, registry));
         } catch (const ReflectionError& error) {
            throw ReflectionError(error.code(),
                                  std::string(name()) + "." + std::string(field.name()) + ": " + error.what());
         } catch (const ValueError& error) {
            throw ReflectionError(ReflectionErrorCode::TypeMismatch,
                                  std::string(name()) + "." + std::string(field.name()) + ": " + error.what());
         }
      }
      return result;
   }

   void TypeDescriptor::populate(void* object, const Value& value, const TypeRegistry& registry) const
   {
      if (!value.is<Value::Object>()) {
         throw ReflectionError(ReflectionErrorCode::TypeMismatch,
                               "Expected object for '" + name_ + "', got " + std::string(value.type_name()));
      }

      // Validate the full shape before writing anything. The public typed
      // conversion then populates a newly constructed object.
      for (const auto& field : fields_) {
         if (!value.contains(field.name())) {
            throw ReflectionError(ReflectionErrorCode::MissingField,
                                  "Missing field '" + name_ + "." + std::string(field.name()) + "'");
         }
      }
      for (const auto& [name, ignored] : value.get<Value::Object>()) {
         (void)ignored;
         if (!find_field(name)) {
            throw ReflectionError(ReflectionErrorCode::UnknownField, "Unknown field '" + name_ + "." + name + "'");
         }
      }

      for (const auto& field : fields_) {
         try {
            field.write_(object, value.at(field.name()), registry);
         } catch (const ReflectionError& error) {
            throw ReflectionError(error.code(), name_ + "." + std::string(field.name()) + ": " + error.what());
         } catch (const ValueError& error) {
            throw ReflectionError(ReflectionErrorCode::TypeMismatch,
                                  name_ + "." + std::string(field.name()) + ": " + error.what());
         }
      }
   }

   const TypeDescriptor* TypeRegistry::find(std::string_view name) const noexcept
   {
      const auto it = by_name_.find(name);
      return it == by_name_.end() ? nullptr : it->second;
   }

   const TypeDescriptor* TypeRegistry::find(std::type_index cpp_type) const noexcept
   {
      const auto it = by_type_.find(cpp_type);
      return it == by_type_.end() ? nullptr : it->second.get();
   }

   Value TypeRegistry::invoke_erased(const TypeDescriptor& type, const void* readonly, void* writable, bool is_rvalue,
                                     std::string_view name, const Value& arguments, std::type_index signature) const
   {
      const MethodDescriptor* method = nullptr;
      if (signature == typeid(void)) {
         auto matches = type.find_methods(name);
         if (matches.empty()) {
            throw ReflectionError(ReflectionErrorCode::UnknownMethod, "Unknown method: " + std::string(name));
         }
         if (matches.size() != 1) {
            throw ReflectionError(ReflectionErrorCode::AmbiguousMethod,
                                  "Ambiguous overloaded method: " + std::string(name) +
                                     "; specify its member-function pointer type");
         }
         method = matches.front();
      } else {
         method = type.find_method(name, signature);
         if (!method) {
            throw ReflectionError(ReflectionErrorCode::UnknownMethod,
                                  "No method matches the requested signature: " + std::string(name));
         }
      }

      if (!method->is_const() && !writable) {
         throw ReflectionError(ReflectionErrorCode::ConstViolation,
                               "Cannot invoke non-const method on a const object: " + std::string(name));
      }
      if ((method->ref_qualifier() == RefQualifier::LValue && is_rvalue) ||
          (method->ref_qualifier() == RefQualifier::RValue && !is_rvalue)) {
         throw ReflectionError(ReflectionErrorCode::RefQualifierMismatch,
                               "Object value category does not match method ref-qualifier: " + std::string(name));
      }

      const auto parameters = method->parameters();
      std::vector<Value> ordered;
      ordered.reserve(parameters.size());
      if (arguments.is<Value::Array>()) {
         const auto& list = arguments.get<Value::Array>();
         if (list.size() != parameters.size()) {
            throw ReflectionError(ReflectionErrorCode::InvalidArguments,
                                  "Wrong number of arguments for: " + std::string(name));
         }
         ordered.assign(list.begin(), list.end());
      } else if (arguments.is<Value::Object>()) {
         const auto& object = arguments.get<Value::Object>();
         for (const auto& param : parameters) {
            if (!arguments.contains(param.name())) {
               throw ReflectionError(ReflectionErrorCode::MissingArgument,
                                     "Missing argument '" + std::string(param.name()) + "'");
            }
            ordered.push_back(arguments.at(param.name()));
         }
         for (const auto& [key, ignored] : object) {
            (void)ignored;
            if (!method->find_parameter(key)) {
               throw ReflectionError(ReflectionErrorCode::UnknownArgument, "Unknown argument '" + key + "'");
            }
         }
      } else {
         throw ReflectionError(ReflectionErrorCode::InvalidArguments, "Arguments must be a Value array or object");
      }
      return method->invoker_(readonly, writable, is_rvalue, ordered, *this);
   }
} // namespace CxxRelay
