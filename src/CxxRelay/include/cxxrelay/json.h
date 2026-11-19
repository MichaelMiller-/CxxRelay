#pragma once

#include <cxxrelay/value.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CxxRelay
{
   enum class JsonErrorCode { InvalidSyntax, InvalidUnicode, DuplicateKey, InvalidNumber, ResourceLimit };

   class JsonError final : public std::runtime_error
   {
   public:
      JsonError(JsonErrorCode code, std::size_t offset, std::string message)
          : std::runtime_error(std::move(message)), code_(code), offset_(offset)
      {
      }
      [[nodiscard]] JsonErrorCode code() const noexcept { return code_; }
      [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

   private:
      JsonErrorCode code_;
      std::size_t offset_;
   };

   // Conservative defaults for untrusted chat/tool payloads. Size limits are in bytes.
   struct JsonOptions
   {
      std::size_t max_depth{128};
      std::size_t max_bytes{4 * 1024 * 1024};
      std::size_t max_nodes{200000};
   };

   // Strict RFC 8259 JSON. No comments, trailing commas or duplicate keys.
   // Integer tokens use int64_t if they fit, otherwise uint64_t (for positives).
   // Note: JSON cannot distinguish uint64_t(42) from int64_t(42) by syntax.
   [[nodiscard]] Value parse_json(std::string_view source, JsonOptions options = {});
   [[nodiscard]] std::string to_json(const Value& value, JsonOptions options = {});

} // namespace CxxRelay
