#pragma once

#include <cxxrelay/error.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace CxxRelay
{
   enum class ValueErrorCode { TypeMismatch, OutOfRange, Overflow, InvalidValue, DuplicateKey };

   using ValueError = Error<ValueErrorCode>;

   //! \brief An owning, transport-friendly value. Does not contain object pointers or handles.
   class Value final
   {
   public:
      using Array = std::vector<Value>;
      using Object = std::map<std::string, Value, std::less<>>;

      // Storage order must match Kind.
      enum class Kind : std::size_t { Null, Bool, Int64, UInt64, Double, String, Array, Object };

      Value() noexcept = default;
      Value(std::nullptr_t) noexcept;
      Value(bool value) noexcept;

      template <std::signed_integral T>
         requires(not std::same_as<T, bool>)
      Value(T value) noexcept : storage_(static_cast<std::int64_t>(value))
      {
      }

      template <std::unsigned_integral T>
         requires(not std::same_as<T, bool>)
      Value(T value) noexcept : storage_(static_cast<std::uint64_t>(value))
      {
      }

      Value(double value);

      template <std::floating_point T>
         requires(not std::same_as<T, double>)
      Value(T value) : Value(static_cast<double>(value))
      {
      }

      Value(std::string value);
      Value(std::string_view value);
      Value(const char* value);
      Value(Array value);
      Value(Object value);

      [[nodiscard]] static Value array(std::initializer_list<Value> entries = {});
      [[nodiscard]] static Value object(std::initializer_list<std::pair<std::string, Value>> entries = {});

      [[nodiscard]] auto kind() const noexcept -> Kind;
      [[nodiscard]] auto type_name() const noexcept -> std::string_view;
      [[nodiscard]] auto is_null() const noexcept -> bool;

      // Exactly typed access. get<T>() does not perform numeric/string conversion.
      // Supported T: bool, int64_t, uint64_t, double, string, Array, Object.
      template <typename T>
      [[nodiscard]] bool is() const noexcept
      {
         return std::holds_alternative<T>(storage_);
      }

      template <typename T>
      [[nodiscard]] auto get() const -> T const&
      {
         if (const T* result = std::get_if<T>(&storage_)) {
            return *result;
         }
         throw ValueError(ValueErrorCode::TypeMismatch,
                          "Value: type mismatch (actual type: " + std::string(type_name()) + ")");
      }

      // Explicit loss-checked conversions. No parsing, double->integer truncation,
      // or precision-loss conversion from integers to double.
      [[nodiscard]] std::int64_t as_int64() const;
      [[nodiscard]] std::uint64_t as_uint64() const;
      [[nodiscard]] double as_double() const;

      [[nodiscard]] std::size_t size() const;
      [[nodiscard]] bool contains(std::string_view key) const;
      [[nodiscard]] const Value& at(std::size_t index) const;
      [[nodiscard]] Value& at(std::size_t index);
      [[nodiscard]] const Value& at(std::string_view key) const;
      [[nodiscard]] Value& at(std::string_view key);

      void push_back(Value element);
      void set(std::string key, Value element);

      friend bool operator==(const Value&, const Value&) = default;

   private:
      //! \note: Storage order must match Kind.
      using Storage =
         std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, Array, Object>;

      Storage storage_{};
   };

} // namespace CxxRelay
