#pragma once

#include <cxxrelay/error.h>
#include <cxxrelay/json.h>
#include <cxxrelay/json_schema.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace CxxRelay
{
   // Errors at registration time are programming/configuration errors.
   enum class ToolRegistrationErrorCode {
      InvalidName,
      DuplicateName,
      MissingDescription,
      NullService,
      UnknownMethod,
      AmbiguousMethod,
      ConstViolation,
      UnsafeMethod
   };

   using ToolRegistrationError = Error<ToolRegistrationErrorCode>;

   enum class ToolErrorCode {
      None,
      UnknownTool,
      PermissionDenied,
      InvalidArguments,
      ConfirmationRequired,
      ConfirmationDenied,
      ExecutionFailed,
      ResponseLimit
   };

   // Distinct tool response: opaque implementation diagnostics are not returned
   // to the model. This is suitable for JSON serialization and future adapters.
   struct ToolResult
   {
      ToolErrorCode code{ToolErrorCode::None};
      Value result{};
      std::string message{};

      [[nodiscard]] bool ok() const noexcept { return code == ToolErrorCode::None; }
      [[nodiscard]] Value to_value() const;
      [[nodiscard]] std::string to_json(JsonOptions options = {}) const;
   };

   // Passed ONLY from trusted application code; never deserialize these flags or
   // callback from an LLM response, JSON request, or untrusted client.
   struct ToolConfirmationRequest
   {
      std::string_view principal;
      std::string_view tool_name;
      const Value& arguments;
   };

   struct ToolContext
   {
      std::string principal{};
      bool allow_read{false};
      bool allow_write{false};
      // Optional per-tool policy, evaluated in addition to allow_read/allow_write.
      // Any exception denies access (including when building definitions).
      std::function<bool(std::string_view, MethodAccess)> allow_tool{};
      // The host must obtain an explicit user confirmation for THIS exact call.
      // The callback may be blocking in this synchronous v0.6 implementation.
      std::function<bool(const ToolConfirmationRequest&)> confirm_write{};
   };

   struct ToolOptions
   {
      // Defaults to the registered method description; must not be empty.
      std::string description{};
   };

   struct ToolAuditEvent
   {
      std::string_view principal;
      std::string_view tool_name;
      ToolErrorCode code;
   };

   // ToolDispatcher borrows the TypeRegistry: it must remain alive, and must not
   // be mutated, for the lifetime of the dispatcher. Service objects are owned
   // by shared_ptr captures, so they cannot dangle after expose().
   // Register and dispatch from a single thread or externally synchronize changes.
   // Concurrent invocation of a service must also be synchronized by the host.
   class ToolDispatcher final
   {
   public:
      explicit ToolDispatcher(const TypeRegistry& registry, JsonOptions limits = {})
          : registry_(registry), limits_(limits)
      {
      }
      ToolDispatcher(const ToolDispatcher&) = delete;
      ToolDispatcher& operator=(const ToolDispatcher&) = delete;
      ToolDispatcher(ToolDispatcher&&) = delete;
      ToolDispatcher& operator=(ToolDispatcher&&) = delete;

      // Only explicitly exposed methods become tools. For overloads, pass the
      // *exact* member function pointer type in signature.
      template <typename Service>
      ToolDispatcher& expose(std::string name, std::shared_ptr<Service> service, std::string_view method_name,
                             ToolOptions options = {}, std::type_index signature = typeid(void));

      // These are provider-neutral definitions. A provider adapter can map
      // {name, description, parameters} into its current tool format.
      [[nodiscard]] Value definitions(const ToolContext& context) const;
      [[nodiscard]] std::size_t size() const noexcept { return tools_.size(); }

      // Source JSON must be an object of named parameters, not a positional array.
      // Runtime errors return ToolResult; registration errors are thrown.
      [[nodiscard]] ToolResult dispatch(std::string_view name, std::string_view arguments_json,
                                        const ToolContext& context) const;

      void set_audit_sink(std::function<void(const ToolAuditEvent&)> sink) { audit_ = std::move(sink); }

   private:
      struct Tool
      {
         std::string description;
         Value parameter_schema;
         MethodAccess access{};
         const MethodDescriptor* method{};
         std::function<Value(const Value&)> invoke;
      };

      ToolDispatcher& add(std::string name, const TypeDescriptor& owner, const MethodDescriptor& method,
                          std::function<Value(const Value&)> invoker, ToolOptions options, bool const_service);
      [[nodiscard]] static bool permitted(const ToolContext& context, std::string_view name,
                                          MethodAccess access) noexcept;
      void audit(std::string_view name, std::string_view principal, ToolErrorCode code) const noexcept;
      [[nodiscard]] ToolResult finish(std::string_view name, const ToolContext& context,
                                      ToolResult result) const noexcept;
      void validate_arguments(const MethodDescriptor& method, const Value& arguments) const;
      void validate_shape(const TypeShape& shape, const Value& value, const std::string& path, std::size_t depth) const;

      const TypeRegistry& registry_;
      JsonOptions limits_;
      std::map<std::string, Tool, std::less<>> tools_;
      std::function<void(const ToolAuditEvent&)> audit_{};
   };

   template <typename Service>
   ToolDispatcher& ToolDispatcher::expose(std::string name, std::shared_ptr<Service> service,
                                          std::string_view method_name, ToolOptions options, std::type_index signature)
   {
      using BareService = std::remove_const_t<Service>;
      static_assert(std::is_class_v<BareService> && !std::is_volatile_v<Service>,
                    "Tool service must be a non-volatile C++ class");
      if (!service)
         throw ToolRegistrationError(ToolRegistrationErrorCode::NullService, "Cannot expose a null service");
      const TypeDescriptor* owner = registry_.find<BareService>();
      if (!owner)
         throw ToolRegistrationError(ToolRegistrationErrorCode::UnknownMethod, "Tool service type is not registered");

      const MethodDescriptor* method = nullptr;
      if (signature == typeid(void)) {
         const auto matches = owner->find_methods(method_name);
         if (matches.size() > 1)
            throw ToolRegistrationError(ToolRegistrationErrorCode::AmbiguousMethod,
                                        "Method is overloaded; provide its exact signature");
         if (!matches.empty())
            method = matches.front();
      } else {
         method = owner->find_method(method_name, signature);
      }
      if (!method)
         throw ToolRegistrationError(ToolRegistrationErrorCode::UnknownMethod,
                                     "Method not registered for this service/signature");

      return add(
         std::move(name), *owner, *method,
         [registry = &registry_, service = std::move(service), method_name = std::string(method_name),
          signature](const Value& args) { return registry->invoke(*service, method_name, args, signature); },
         std::move(options), std::is_const_v<Service>);
   }
} // namespace CxxRelay
