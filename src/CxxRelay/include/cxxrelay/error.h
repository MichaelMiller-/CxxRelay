#pragma once

#include <stdexcept>
#include <type_traits>

namespace CxxRelay
{
   template <typename T>
      requires std::is_enum_v<T>
   class Error final : public std::runtime_error
   {
      T code_;

   public:
      Error(T code, std::string_view message) : std::runtime_error(message.data()), code_(code) {}

      [[nodiscard]] T code() const noexcept { return code_; }
   };
} // namespace CxxRelay