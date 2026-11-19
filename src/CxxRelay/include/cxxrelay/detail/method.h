#pragma once

// Included by reflection.hpp after TypeRegistry and TypeBuilder are complete.
#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace CxxRelay::detail
{
   // Preserve cv, ref and noexcept qualifiers of the member pointer. No
   // specialization is provided for volatile members; those are unsupported.
   template <typename>
   struct member_function_traits;

#define REFLECTION_MEMBER_TRAITS(CV, REF, NOEX, C, RQ, NX)                                                             \
   template <class Owner, class Return, class... Args>                                                                 \
   struct member_function_traits<Return (Owner::*)(Args...) CV REF NOEX>                                               \
   {                                                                                                                   \
      using class_type = Owner;                                                                                        \
      using return_type = Return;                                                                                      \
      using args_tuple = std::tuple<Args...>;                                                                          \
      static constexpr bool is_const = C;                                                                              \
      static constexpr bool is_noexcept = NX;                                                                          \
      static constexpr RefQualifier ref_qualifier = RefQualifier::RQ;                                                  \
   };

   REFLECTION_MEMBER_TRAITS(, , , false, None, false)
   REFLECTION_MEMBER_TRAITS(const, , , true, None, false)
   REFLECTION_MEMBER_TRAITS(, &, , false, LValue, false)
   REFLECTION_MEMBER_TRAITS(const, &, , true, LValue, false)
   REFLECTION_MEMBER_TRAITS(, &&, , false, RValue, false)
   REFLECTION_MEMBER_TRAITS(const, &&, , true, RValue, false)
   REFLECTION_MEMBER_TRAITS(, , noexcept, false, None, true)
   REFLECTION_MEMBER_TRAITS(const, , noexcept, true, None, true)
   REFLECTION_MEMBER_TRAITS(, &, noexcept, false, LValue, true)
   REFLECTION_MEMBER_TRAITS(const, &, noexcept, true, LValue, true)
   REFLECTION_MEMBER_TRAITS(, &&, noexcept, false, RValue, true)
   REFLECTION_MEMBER_TRAITS(const, &&, noexcept, true, RValue, true)

#undef REFLECTION_MEMBER_TRAITS

   template <class T>
   constexpr PassingMode passing_mode() noexcept
   {
      if constexpr (std::is_lvalue_reference_v<T>) {
         if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
            return PassingMode::ConstLValueReference;
         } else {
            return PassingMode::LValueReference;
         }
      } else if constexpr (std::is_rvalue_reference_v<T>) {
         if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
            return PassingMode::ConstRValueReference;
         } else {
            return PassingMode::RValueReference;
         }
      } else {
         return PassingMode::Value;
      }
   }

   // Inbound Value arguments are owned local values. const T& can bind to those
   // locals and T&& can consume them; T& cannot have observable copy-out behavior,
   // so such signatures are registered for introspection but not invocable.
   // Probe whether our Step-1/2 codecs can be instantiated for a type.
   // Unknown CLASS types may be registered with the registry at runtime, so
   // their availability cannot be decided here. Pointer/enum and non-default-
   // constructible parameter types stay valid as method metadata, but cannot be
   // marshalled automatically through Value.
   template <class U, bool Decode>
   consteval bool codec_compatible()
   {
      using T = std::remove_cvref_t<U>;
      if constexpr (std::is_same_v<T, Value> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, bool> ||
                    std::is_same_v<T, std::string>) {
         return true;
      } else if constexpr (std::is_integral_v<T>) {
         return sizeof(T) <= sizeof(std::uint64_t);
      } else if constexpr (std::is_floating_point_v<T>) {
         return sizeof(T) <= sizeof(double);
      } else if constexpr (vector_traits<T>::value) {
         return codec_compatible<typename vector_traits<T>::item_type, Decode>();
      } else if constexpr (optional_traits<T>::value) {
         return codec_compatible<typename optional_traits<T>::item_type, Decode>();
      } else if constexpr (std::is_class_v<T> && !std::is_union_v<T>) {
         return !Decode || std::default_initializable<T>;
      } else {
         return false;
      }
   }

   template <class Tuple, std::size_t... I>
   consteval bool all_decodable(std::index_sequence<I...>)
   {
      return (codec_compatible<std::tuple_element_t<I, Tuple>, true>() && ...);
   }

   template <class T>
   inline constexpr bool mutable_lvalue_argument_v =
      std::is_lvalue_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>;

   template <class Tuple, std::size_t... I>
   consteval bool has_mutable_lvalue_arguments(std::index_sequence<I...>)
   {
      return (mutable_lvalue_argument_v<std::tuple_element_t<I, Tuple>> || ...);
   }

   template <class Param>
   std::remove_cvref_t<Param> decode_argument(const Value& source, const TypeRegistry& registry,
                                              std::string_view parameter_name)
   {
      try {
         return registry.from_value<std::remove_cvref_t<Param>>(source);
      } catch (const ReflectionError& error) {
         throw ReflectionError(error.code(), "Parameter '" + std::string(parameter_name) + "': " + error.what());
      } catch (const ValueError& error) {
         throw ReflectionError(translate(error).code(),
                               "Parameter '" + std::string(parameter_name) + "': " + error.what());
      }
   }

   template <class MethodPointer, class Instance, class... Args>
   decltype(auto) call_member(MethodPointer method, Instance& object, Args&&... args)
   {
      using Traits = member_function_traits<MethodPointer>;
      if constexpr (Traits::ref_qualifier == RefQualifier::RValue) {
         return std::invoke(method, std::move(object), std::forward<Args>(args)...);
      } else {
         return std::invoke(method, object, std::forward<Args>(args)...);
      }
   }

   template <class MethodPointer, class T, class Tuple, std::size_t... I>
   Value call_decoded(MethodPointer pointer, T& object, const TypeRegistry& registry, Tuple& decoded,
                      std::index_sequence<I...>)
   {
      using Traits = member_function_traits<MethodPointer>;
      using Parameters = typename Traits::args_tuple;
      using Return = typename Traits::return_type;
      // Only the actual application call is enclosed by this exception
      // boundary. Errors from result serialisation remain codec errors.
      auto checked_call = [&]() -> Return {
         try {
            return call_member(pointer, object,
                               std::forward<std::tuple_element_t<I, Parameters>>(std::get<I>(decoded))...);
         } catch (...) {
            std::throw_with_nested(
               ReflectionError(ReflectionErrorCode::InvocationFailed, "Registered C++ method raised an exception"));
         }
      };
      if constexpr (std::is_void_v<Return>) {
         checked_call();
         return Value(nullptr);
      } else {
         // Reference returns are materialised before the target is released.
         // The returned Value never contains a borrowed object reference.
         decltype(auto) result = checked_call();
         return registry.to_value(result);
      }
   }

   template <class T, class MethodPointer>
   Value invoke_member(MethodPointer pointer, const void* readonly, void* writable, bool /*is_rvalue*/,
                       std::span<const Value> inputs, const TypeRegistry& registry,
                       const std::vector<std::string>& parameter_names)
   {
      using Traits = member_function_traits<MethodPointer>;
      using Parameters = typename Traits::args_tuple;
      constexpr std::size_t n = std::tuple_size_v<Parameters>;
      constexpr auto indices = std::make_index_sequence<n>{};

      if constexpr (has_mutable_lvalue_arguments<Parameters>(indices)) {
         throw ReflectionError(ReflectionErrorCode::UnsupportedParameter,
                               "Mutable T& parameters cannot be transported as Value arguments");
      } else if constexpr (!all_decodable<Parameters>(indices)) {
         throw ReflectionError(ReflectionErrorCode::UnsupportedParameter,
                               "Parameter type has no supported Value conversion");
      } else if constexpr (!std::is_void_v<typename Traits::return_type> &&
                           !codec_compatible<typename Traits::return_type, false>()) {
         throw ReflectionError(ReflectionErrorCode::UnsupportedReturn, "Return type has no supported Value conversion");
      } else {
         // Decode all parameters before calling the target. A conversion error
         // cannot cause the target to be executed with partial arguments.
         auto decoded = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::tuple<std::remove_cvref_t<std::tuple_element_t<I, Parameters>>...>{
               decode_argument<std::tuple_element_t<I, Parameters>>(inputs[I], registry, parameter_names[I])...};
         }(indices);

         try {
            if constexpr (Traits::is_const) {
               return call_decoded(pointer, *static_cast<const T*>(readonly), registry, decoded, indices);
            } else {
               return call_decoded(pointer, *static_cast<T*>(writable), registry, decoded, indices);
            }
         } catch (const ReflectionError&) {
            throw;
         } catch (const ValueError& error) {
            throw translate(error);
         }
      }
   }

} // namespace CxxRelay::detail

namespace CxxRelay
{
   template <class T>
   template <class MethodPointer>
   TypeBuilder<T>& TypeBuilder<T>::method(std::string name, MethodPointer pointer, MethodOptions options)
   {
      static_assert(std::is_member_function_pointer_v<MethodPointer>,
                    "method() requires a pointer to a non-static member function");
      using Traits = detail::member_function_traits<MethodPointer>;
      static_assert(std::is_same_v<typename Traits::class_type, T>,
                    "Method must be declared on the registered class T");
      // The expression is intentionally checked even though invocation will
      // be implemented in Step 4; this prevents registering a null pointer.
      if (pointer == nullptr) {
         throw ReflectionError(ReflectionErrorCode::InvalidMethodPointer,
                               "Cannot register a null member-function pointer");
      }

      using Parameters = typename Traits::args_tuple;
      constexpr std::size_t count = std::tuple_size_v<Parameters>;
      if (!options.parameter_names.empty() && options.parameter_names.size() != count) {
         throw ReflectionError(ReflectionErrorCode::InvalidParameterNames,
                               "Parameter name count does not match method arity: " + name);
      }
      if (options.parameter_names.empty()) {
         options.parameter_names.reserve(count);
         for (std::size_t i = 0; i < count; ++i) {
            options.parameter_names.push_back("arg" + std::to_string(i));
         }
      }
      for (std::size_t i = 0; i < options.parameter_names.size(); ++i) {
         if (options.parameter_names[i].empty()) {
            throw ReflectionError(ReflectionErrorCode::InvalidParameterNames,
                                  "Parameter names must not be empty: " + name);
         }
         for (std::size_t j = 0; j < i; ++j) {
            if (options.parameter_names[j] == options.parameter_names[i]) {
               throw ReflectionError(ReflectionErrorCode::InvalidParameterNames,
                                     "Duplicate parameter name: " + options.parameter_names[i]);
            }
         }
      }

      std::vector<ParameterDescriptor> parameters;
      parameters.reserve(count);
      [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
         (parameters.push_back(ParameterDescriptor{
             std::move(options.parameter_names[Indices]),
             std::type_index(typeid(std::remove_cvref_t<std::tuple_element_t<Indices, Parameters>>)),
             detail::passing_mode<std::tuple_element_t<Indices, Parameters>>(),
             detail::shape_of<std::tuple_element_t<Indices, Parameters>>()}),
          ...);
      }(std::make_index_sequence<count>{});

      using Return = typename Traits::return_type;
      // Capture parameter labels for contextual conversion diagnostics, BEFORE
      // moving them into the reflection descriptor.
      std::vector<std::string> labels;
      labels.reserve(parameters.size());
      for (const auto& parameter : parameters) {
         labels.emplace_back(parameter.name());
      }
      MethodDescriptor::Invoker invoker =
         [pointer, labels = std::move(labels)](const void* readonly, void* writable, bool is_rvalue,
                                               std::span<const Value> inputs, const TypeRegistry& registry) -> Value {
         return detail::invoke_member<T>(pointer, readonly, writable, is_rvalue, inputs, registry, labels);
      };
      descriptor_->add_method(
         MethodDescriptor{std::move(name), std::move(options.description),
                          options.access.value_or(Traits::is_const ? MethodAccess::ReadOnly : MethodAccess::Write),
                          std::type_index(typeid(std::remove_cvref_t<Return>)), detail::passing_mode<Return>(),
                          detail::shape_of<Return>(), Traits::is_const, Traits::is_noexcept, Traits::ref_qualifier,
                          std::type_index(typeid(MethodPointer)), std::move(parameters), std::move(invoker)});
      return *this;
   }
} // namespace CxxRelay
