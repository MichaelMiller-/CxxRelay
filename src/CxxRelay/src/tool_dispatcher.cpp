#include <cxxrelay/tool_dispatcher.h>

namespace CxxRelay
{
   namespace
   {
      std::string_view error_name(ToolErrorCode code) noexcept
      {
         switch (code) {
         case ToolErrorCode::None:
            return "none";
         case ToolErrorCode::UnknownTool:
            return "unknown_tool";
         case ToolErrorCode::PermissionDenied:
            return "permission_denied";
         case ToolErrorCode::InvalidArguments:
            return "invalid_arguments";
         case ToolErrorCode::ConfirmationRequired:
            return "confirmation_required";
         case ToolErrorCode::ConfirmationDenied:
            return "confirmation_denied";
         case ToolErrorCode::ExecutionFailed:
            return "execution_failed";
         case ToolErrorCode::ResponseLimit:
            return "response_limit";
         }
         return "execution_failed";
      }

      bool valid_name(std::string_view name) noexcept
      {
         if (name.empty() || name.size() > 64)
            return false;
         const auto letter = [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
         };
         if (!letter(static_cast<unsigned char>(name.front())))
            return false;
         for (unsigned char c : name) {
            if (!letter(c) && !(c >= '0' && c <= '9') && c != '-')
               return false;
         }
         return true;
      }

      [[noreturn]] void bad_arg(const std::string& path, std::string_view what)
      {
         throw std::invalid_argument("Invalid tool argument at '" + path + "': " + std::string(what));
      }

      // We use the exact Value integer comparison rather than converting to double,
      // which would lose precision for large 64-bit values.
      void check_integer(const Value& value, const TypeShape& shape, const std::string& path, bool is_signed)
      {
         if (!value.is<std::int64_t>() && !value.is<std::uint64_t>())
            bad_arg(path, "expected integer");
         try {
            if (is_signed) {
               const auto n = value.as_int64();
               if (shape.minimum && n < shape.minimum->as_int64())
                  bad_arg(path, "below minimum");
               if (shape.maximum && n > shape.maximum->as_int64())
                  bad_arg(path, "above maximum");
            } else {
               const auto n = value.as_uint64();
               if (shape.minimum && n < shape.minimum->as_uint64())
                  bad_arg(path, "below minimum");
               if (shape.maximum && n > shape.maximum->as_uint64())
                  bad_arg(path, "above maximum");
            }
         } catch (const ValueError&) {
            bad_arg(path, "integer is out of range");
         }
      }

      ToolResult failure(ToolErrorCode code, std::string message)
      {
         return {.code = code, .result = Value(nullptr), .message = std::move(message)};
      }

   } // namespace

   Value ToolResult::to_value() const
   {
      if (ok())
         return Value::object({{"ok", true}, {"result", result}});
      return Value::object(
         {{"ok", false}, {"error", Value::object({{"code", std::string(error_name(code))}, {"message", message}})}});
   }

   std::string ToolResult::to_json(JsonOptions options) const { return CxxRelay::to_json(to_value(), options); }

   ToolDispatcher& ToolDispatcher::add(std::string name, const TypeDescriptor& owner, const MethodDescriptor& method,
                                       std::function<Value(const Value&)> invoker, ToolOptions options,
                                       bool const_service)
   {
      if (!valid_name(name))
         throw ToolRegistrationError(ToolRegistrationErrorCode::InvalidName,
                                     "Tool name must match [A-Za-z_][A-Za-z0-9_-]{0,63}");
      if (tools_.contains(name))
         throw ToolRegistrationError(ToolRegistrationErrorCode::DuplicateName, "Tool name already registered: " + name);
      if (options.description.empty())
         options.description = method.description();
      if (options.description.empty())
         throw ToolRegistrationError(ToolRegistrationErrorCode::MissingDescription,
                                     "Tool requires a non-empty description");
      if (const_service && !method.is_const())
         throw ToolRegistrationError(ToolRegistrationErrorCode::ConstViolation,
                                     "Cannot expose a mutable method on a const service");
      if (method.ref_qualifier() == RefQualifier::RValue ||
          (method.access() == MethodAccess::ReadOnly && !method.is_const()))
         throw ToolRegistrationError(ToolRegistrationErrorCode::UnsafeMethod,
                                     "Unsafe method for externally callable tool");

      Value parameter_schema;
      try {
         JsonSchemaGenerator generator(registry_);
         parameter_schema = generator.arguments(owner, method);
         (void)generator.result(owner, method); // Fail early on unsupported result types.
      } catch (const SchemaError& error) {
         throw ToolRegistrationError(ToolRegistrationErrorCode::UnsafeMethod,
                                     std::string("Untransportable method: ") + error.what());
      }
      tools_.emplace(std::move(name), Tool{std::move(options.description), std::move(parameter_schema), method.access(),
                                           &method, std::move(invoker)});
      return *this;
   }

   bool ToolDispatcher::permitted(const ToolContext& context, std::string_view name, MethodAccess access) noexcept
   {
      const bool has_role = access == MethodAccess::Write ? context.allow_write : context.allow_read;
      if (!has_role)
         return false;
      if (!context.allow_tool)
         return true;
      try {
         return context.allow_tool(name, access);
      } catch (...) {
         return false;
      }
   }

   Value ToolDispatcher::definitions(const ToolContext& context) const
   {
      Value definitions = Value::array();
      for (const auto& [name, tool] : tools_) {
         const bool writable = tool.access == MethodAccess::Write;
         if (!permitted(context, name, tool.access))
            continue;
         definitions.push_back(Value::object({{"name", name},
                                              {"description", tool.description},
                                              {"parameters", tool.parameter_schema},
                                              {"access", writable ? "write" : "read_only"},
                                              {"requires_confirmation", writable}}));
      }
      return definitions;
   }

   void ToolDispatcher::audit(std::string_view name, std::string_view principal, ToolErrorCode code) const noexcept
   {
      if (!audit_)
         return;
      try {
         audit_(ToolAuditEvent{principal, name, code});
      } catch (...) {
         // Audit sinks must not corrupt the tool result. Production systems
         // should report sink failures through a separate monitoring channel.
      }
   }

   ToolResult ToolDispatcher::finish(std::string_view name, const ToolContext& context,
                                     ToolResult result) const noexcept
   {
      audit(name, context.principal, result.code);
      return result;
   }

   void ToolDispatcher::validate_shape(const TypeShape& shape, const Value& value, const std::string& path,
                                       std::size_t depth) const
   {
      if (depth > limits_.max_depth)
         bad_arg(path, "maximum nesting depth exceeded");
      switch (shape.kind) {
      case TypeShape::Kind::Any:
         return;
      case TypeShape::Kind::Void:
      case TypeShape::Kind::Null:
         if (!value.is_null())
            bad_arg(path, "expected null");
         return;
      case TypeShape::Kind::Boolean:
         if (!value.is<bool>())
            bad_arg(path, "expected boolean");
         return;
      case TypeShape::Kind::String:
         if (!value.is<std::string>())
            bad_arg(path, "expected string");
         return;
      case TypeShape::Kind::Number:
         try {
            (void)value.as_double();
         } catch (const ValueError&) {
            bad_arg(path, "expected exactly representable number");
         }
         return;
      case TypeShape::Kind::SignedInteger:
         check_integer(value, shape, path, true);
         return;
      case TypeShape::Kind::UnsignedInteger:
         check_integer(value, shape, path, false);
         return;
      case TypeShape::Kind::Optional:
         if (value.is_null())
            return;
         if (!shape.element)
            bad_arg(path, "invalid optional type");
         return validate_shape(*shape.element, value, path, depth + 1);
      case TypeShape::Kind::Array: {
         if (!value.is<Value::Array>())
            bad_arg(path, "expected array");
         if (!shape.element)
            bad_arg(path, "invalid array type");
         std::size_t i = 0;
         for (const Value& child : value.get<Value::Array>()) {
            validate_shape(*shape.element, child, path + "[" + std::to_string(i++) + "]", depth + 1);
         }
         return;
      }
      case TypeShape::Kind::Registered: {
         if (!value.is<Value::Object>())
            bad_arg(path, "expected object");
         const auto* owner = registry_.find(shape.cpp_type);
         if (!owner)
            bad_arg(path, "unregistered object type");
         for (const auto& field : owner->fields()) {
            if (!value.contains(field.name()))
               bad_arg(path + "." + std::string(field.name()), "missing field");
            validate_shape(field.shape(), value.at(field.name()), path + "." + std::string(field.name()), depth + 1);
         }
         for (const auto& [key, unused] : value.get<Value::Object>()) {
            (void)unused;
            if (!owner->find_field(key))
               bad_arg(path + "." + key, "unknown field");
         }
         return;
      }
      case TypeShape::Kind::Unsupported:
         bad_arg(path, "unsupported value type");
      }
   }

   void ToolDispatcher::validate_arguments(const MethodDescriptor& method, const Value& arguments) const
   {
      if (!arguments.is<Value::Object>())
         bad_arg("$", "expected a named-argument object");
      for (const auto& parameter : method.parameters()) {
         const auto name = std::string(parameter.name());
         if (!arguments.contains(name))
            bad_arg(name, "missing required argument");
         validate_shape(parameter.shape(), arguments.at(name), name, 0);
      }
      for (const auto& [name, ignored] : arguments.get<Value::Object>()) {
         (void)ignored;
         if (!method.find_parameter(name))
            bad_arg(name, "unknown argument");
      }
   }

   ToolResult ToolDispatcher::dispatch(std::string_view name, std::string_view arguments_json,
                                       const ToolContext& context) const
   {
      const auto it = tools_.find(name);
      if (it == tools_.end())
         return finish(name, context, failure(ToolErrorCode::UnknownTool, "Unknown tool"));
      const auto& tool = it->second;
      const bool writable = tool.access == MethodAccess::Write;
      if (!permitted(context, name, tool.access))
         return finish(name, context, failure(ToolErrorCode::PermissionDenied, "Not permitted to call this tool"));

      Value arguments;
      try {
         arguments = parse_json(arguments_json, limits_);
         validate_arguments(*tool.method, arguments);
      } catch (const JsonError&) {
         return finish(name, context,
                       failure(ToolErrorCode::InvalidArguments, "Invalid JSON arguments or input limits exceeded"));
      } catch (const std::invalid_argument&) {
         return finish(name, context,
                       failure(ToolErrorCode::InvalidArguments, "Arguments do not match the tool schema"));
      } catch (const ValueError&) {
         return finish(name, context, failure(ToolErrorCode::InvalidArguments, "Invalid argument value"));
      }

      if (writable) {
         if (!context.confirm_write)
            return finish(name, context, failure(ToolErrorCode::ConfirmationRequired, "User confirmation is required"));
         bool approved = false;
         try {
            approved = context.confirm_write(ToolConfirmationRequest{context.principal, name, arguments});
         } catch (...) {
            approved = false;
         }
         if (!approved)
            return finish(name, context,
                          failure(ToolErrorCode::ConfirmationDenied, "User did not approve the operation"));
      }

      try {
         Value result = tool.invoke(arguments);
         ToolResult output{ToolErrorCode::None, std::move(result), {}};
         // Enforce serialisability, depth/byte/node limits before returning it.
         // Note: write methods may have ALREADY modified application state.
         (void)output.to_json(limits_);
         return finish(name, context, std::move(output));
      } catch (const JsonError& error) {
         if (error.code() == JsonErrorCode::ResourceLimit)
            return finish(name, context, failure(ToolErrorCode::ResponseLimit, "Tool response exceeds JSON limits"));
         return finish(name, context,
                       failure(ToolErrorCode::ExecutionFailed, "Application returned an invalid JSON value"));
      } catch (const ReflectionError& error) {
         switch (error.code()) {
         case ReflectionErrorCode::InvalidArguments:
         case ReflectionErrorCode::MissingArgument:
         case ReflectionErrorCode::UnknownArgument:
         case ReflectionErrorCode::TypeMismatch:
         case ReflectionErrorCode::OutOfRange:
            return finish(name, context,
                          failure(ToolErrorCode::InvalidArguments, "Arguments cannot be converted to C++ parameters"));
         default:
            return finish(name, context, failure(ToolErrorCode::ExecutionFailed, "Application tool failed"));
         }
      } catch (...) {
         return finish(name, context, failure(ToolErrorCode::ExecutionFailed, "Application tool failed"));
      }
   }

} // namespace CxxRelay
