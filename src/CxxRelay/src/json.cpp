#include <cxxrelay/json.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace CxxRelay
{
   namespace
   {
      // UTF-8 validation also rejects overlong encodings, surrogate codepoints,
      // and values above U+10FFFF. JSON is a Unicode data interchange format.
      bool valid_utf8(std::string_view s) noexcept
      {
         for (std::size_t i = 0; i < s.size();) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
               ++i;
               continue;
            }
            std::size_t more = 0;
            std::uint32_t scalar = 0;
            std::uint32_t minimum = 0;
            if ((c & 0xE0) == 0xC0) {
               more = 1;
               scalar = c & 0x1F;
               minimum = 0x80;
            } else if ((c & 0xF0) == 0xE0) {
               more = 2;
               scalar = c & 0x0F;
               minimum = 0x800;
            } else if ((c & 0xF8) == 0xF0) {
               more = 3;
               scalar = c & 0x07;
               minimum = 0x10000;
            } else {
               return false;
            }
            if (i + more >= s.size()) {
               return false;
            }
            for (decltype(more) j = 1; j <= more; ++j) {
               const auto next = static_cast<unsigned char>(s[i + j]);
               if ((next & 0xC0) != 0x80) {
                  return false;
               }
               scalar = (scalar << 6) | (next & 0x3F);
            }
            if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) {
               return false;
            }
            i += more + 1;
         }
         return true;
      }

      void append_utf8(std::string& out, std::uint32_t codepoint)
      {
         if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
         } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
         } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
         } else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
         }
      }

      class Parser final
      {
      public:
         Parser(std::string_view input, JsonOptions options) : input_(input), options_(options) {}
         Value parse()
         {
            if (input_.size() > options_.max_bytes)
               fail(JsonErrorCode::ResourceLimit, "JSON input too large");
            Value result = value(0);
            whitespace();
            if (pos_ != input_.size())
               fail(JsonErrorCode::InvalidSyntax, "Trailing content");
            return result;
         }

      private:
         [[noreturn]] void fail(JsonErrorCode code, std::string_view what) const
         {
            throw JsonError(code, pos_, std::string(what) + " at byte " + std::to_string(pos_));
         }
         char peek() const noexcept { return pos_ < input_.size() ? input_[pos_] : '\0'; }
         void whitespace() noexcept
         {
            while (pos_ < input_.size() &&
                   (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n' || input_[pos_] == '\r'))
               ++pos_;
         }
         bool consume(char c) noexcept
         {
            if (pos_ < input_.size() && input_[pos_] == c) {
               ++pos_;
               return true;
            }
            return false;
         }
         void require(char c)
         {
            if (!consume(c))
               fail(JsonErrorCode::InvalidSyntax, "Expected JSON delimiter");
         }
         void keyword(std::string_view literal)
         {
            if (input_.substr(pos_, literal.size()) != literal)
               fail(JsonErrorCode::InvalidSyntax, "Invalid JSON literal");
            pos_ += literal.size();
         }
         std::uint32_t hex4()
         {
            if (input_.size() - pos_ < 4)
               fail(JsonErrorCode::InvalidUnicode, "Short Unicode escape");
            std::uint32_t result = 0;
            for (int k = 0; k < 4; ++k) {
               char c = input_[pos_++];
               result <<= 4;
               if (c >= '0' && c <= '9')
                  result |= static_cast<std::uint32_t>(c - '0');
               else if (c >= 'a' && c <= 'f')
                  result |= static_cast<std::uint32_t>(c - 'a' + 10);
               else if (c >= 'A' && c <= 'F')
                  result |= static_cast<std::uint32_t>(c - 'A' + 10);
               else
                  fail(JsonErrorCode::InvalidUnicode, "Invalid Unicode hex digit");
            }
            return result;
         }
         std::string string()
         {
            require('"');
            std::string out;
            while (pos_ < input_.size()) {
               const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
               if (c == '"') {
                  if (!valid_utf8(out))
                     fail(JsonErrorCode::InvalidUnicode, "Invalid UTF-8");
                  return out;
               }
               if (c < 0x20)
                  fail(JsonErrorCode::InvalidSyntax, "Unescaped control character");
               if (c != '\\') {
                  out.push_back(static_cast<char>(c));
                  continue;
               }
               if (pos_ == input_.size())
                  fail(JsonErrorCode::InvalidSyntax, "Unfinished escape");
               const char escaped = input_[pos_++];
               switch (escaped) {
               case '"':
                  out.push_back('"');
                  break;
               case '\\':
                  out.push_back('\\');
                  break;
               case '/':
                  out.push_back('/');
                  break;
               case 'b':
                  out.push_back('\b');
                  break;
               case 'f':
                  out.push_back('\f');
                  break;
               case 'n':
                  out.push_back('\n');
                  break;
               case 'r':
                  out.push_back('\r');
                  break;
               case 't':
                  out.push_back('\t');
                  break;
               case 'u': {
                  std::uint32_t codepoint = hex4();
                  if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                     if (!consume('\\') || !consume('u'))
                        fail(JsonErrorCode::InvalidUnicode, "Missing low surrogate");
                     const std::uint32_t low = hex4();
                     if (low < 0xDC00 || low > 0xDFFF)
                        fail(JsonErrorCode::InvalidUnicode, "Invalid low surrogate");
                     codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                  } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                     fail(JsonErrorCode::InvalidUnicode, "Unpaired low surrogate");
                  }
                  append_utf8(out, codepoint);
                  break;
               }
               default:
                  fail(JsonErrorCode::InvalidSyntax, "Invalid escape sequence");
               }
            }
            fail(JsonErrorCode::InvalidSyntax, "Unterminated JSON string");
         }
         Value number()
         {
            const std::size_t begin = pos_;
            const bool negative = consume('-');
            if (consume('0')) {
               if (peek() >= '0' && peek() <= '9')
                  fail(JsonErrorCode::InvalidNumber, "Leading zeros are forbidden");
            } else {
               if (peek() < '1' || peek() > '9')
                  fail(JsonErrorCode::InvalidNumber, "Expected digit");
               do {
                  ++pos_;
               } while (peek() >= '0' && peek() <= '9');
            }
            bool decimal = false;
            if (consume('.')) {
               decimal = true;
               if (peek() < '0' || peek() > '9')
                  fail(JsonErrorCode::InvalidNumber, "Missing fraction digits");
               do {
                  ++pos_;
               } while (peek() >= '0' && peek() <= '9');
            }
            if (peek() == 'e' || peek() == 'E') {
               decimal = true;
               ++pos_;
               if (peek() == '-' || peek() == '+')
                  ++pos_;
               if (peek() < '0' || peek() > '9')
                  fail(JsonErrorCode::InvalidNumber, "Missing exponent digits");
               do {
                  ++pos_;
               } while (peek() >= '0' && peek() <= '9');
            }
            const char* first = input_.data() + begin;
            const char* last = input_.data() + pos_;
            if (!decimal) {
               if (negative) {
                  std::int64_t n{};
                  const auto [end, err] = std::from_chars(first, last, n);
                  if (err == std::errc{} && end == last)
                     return Value(n);
               } else {
                  std::uint64_t n{};
                  const auto [end, err] = std::from_chars(first, last, n);
                  if (err == std::errc{} && end == last) {
                     if (n <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                        return Value(static_cast<std::int64_t>(n));
                     return Value(n);
                  }
               }
               fail(JsonErrorCode::InvalidNumber, "Integer outside 64-bit range");
            }
            double n{};
            const auto [end, err] = std::from_chars(first, last, n, std::chars_format::general);
            if (err != std::errc{} || end != last || !std::isfinite(n))
               fail(JsonErrorCode::InvalidNumber, "Floating-point value cannot be represented");
            return Value(n);
         }
         Value value(std::size_t depth)
         {
            whitespace();
            if (++nodes_ > options_.max_nodes || depth > options_.max_depth)
               fail(JsonErrorCode::ResourceLimit, "JSON resource limit exceeded");
            switch (peek()) {
            case 'n':
               keyword("null");
               return Value(nullptr);
            case 't':
               keyword("true");
               return Value(true);
            case 'f':
               keyword("false");
               return Value(false);
            case '"':
               return Value(string());
            case '[': {
               ++pos_;
               Value::Array a;
               whitespace();
               if (consume(']'))
                  return Value(std::move(a));
               while (true) {
                  a.push_back(value(depth + 1));
                  whitespace();
                  if (consume(']'))
                     break;
                  require(',');
                  whitespace();
                  if (peek() == ']')
                     fail(JsonErrorCode::InvalidSyntax, "Trailing array comma");
               }
               return Value(std::move(a));
            }
            case '{': {
               ++pos_;
               Value::Object o;
               whitespace();
               if (consume('}'))
                  return Value(std::move(o));
               while (true) {
                  if (peek() != '"')
                     fail(JsonErrorCode::InvalidSyntax, "Expected object key");
                  auto key = string();
                  whitespace();
                  require(':');
                  Value item = value(depth + 1);
                  if (!o.emplace(std::move(key), std::move(item)).second)
                     fail(JsonErrorCode::DuplicateKey, "Duplicate JSON key");
                  whitespace();
                  if (consume('}'))
                     break;
                  require(',');
                  whitespace();
                  if (peek() == '}')
                     fail(JsonErrorCode::InvalidSyntax, "Trailing object comma");
               }
               return Value(std::move(o));
            }
            default:
               if (peek() == '-' || (peek() >= '0' && peek() <= '9'))
                  return number();
               fail(JsonErrorCode::InvalidSyntax, "Expected JSON value");
            }
         }
         std::string_view input_;
         JsonOptions options_;
         std::size_t pos_{};
         std::size_t nodes_{};
      };

      class Writer final
      {
      public:
         explicit Writer(JsonOptions options) : options_(options) {}
         std::string dump(const Value& v)
         {
            value(v, 0);
            return std::move(out_);
         }

      private:
         [[noreturn]] void fail(JsonErrorCode code, std::string_view msg) const
         {
            throw JsonError(code, out_.size(), std::string(msg));
         }
         void append(std::string_view part)
         {
            if (part.size() > options_.max_bytes || out_.size() > options_.max_bytes - part.size())
               fail(JsonErrorCode::ResourceLimit, "JSON output too large");
            out_.append(part);
         }
         void character(char c) { append(std::string_view(&c, 1)); }
         void quoted(std::string_view s)
         {
            if (!valid_utf8(s))
               fail(JsonErrorCode::InvalidUnicode, "Invalid UTF-8 in Value string");
            character('"');
            constexpr char hex[] = "0123456789abcdef";
            for (const unsigned char c : s) {
               switch (c) {
               case '"':
                  append("\\\"");
                  break;
               case '\\':
                  append("\\\\");
                  break;
               case '\b':
                  append("\\b");
                  break;
               case '\f':
                  append("\\f");
                  break;
               case '\n':
                  append("\\n");
                  break;
               case '\r':
                  append("\\r");
                  break;
               case '\t':
                  append("\\t");
                  break;
               default:
                  if (c < 0x20) {
                     char escaped[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                     append(std::string_view(escaped, 6));
                  } else
                     character(static_cast<char>(c));
               }
            }
            character('"');
         }
         template <class T>
         void integral(T v)
         {
            char buf[32];
            const auto [end, err] = std::to_chars(std::begin(buf), std::end(buf), v);
            if (err != std::errc{})
               fail(JsonErrorCode::InvalidNumber, "Integer serialization failed");
            append(std::string_view(buf, end - buf));
         }
         void floating(double n)
         {
            char buf[128];
            const auto [end, err] = std::to_chars(std::begin(buf), std::end(buf), n, std::chars_format::general);
            if (err != std::errc{})
               fail(JsonErrorCode::InvalidNumber, "Double serialization failed");
            auto repr = std::string_view(buf, end - buf);
            append(repr);
            // Preserve double vs integer during parsing, where possible.
            if (repr.find_first_of(".eE") == std::string_view::npos)
               append(".0");
         }
         void value(const Value& v, std::size_t depth)
         {
            if (++nodes_ > options_.max_nodes || depth > options_.max_depth)
               fail(JsonErrorCode::ResourceLimit, "JSON output resource limit exceeded");
            switch (v.kind()) {
            case Value::Kind::Null:
               append("null");
               break;
            case Value::Kind::Bool:
               append(v.get<bool>() ? "true" : "false");
               break;
            case Value::Kind::Int64:
               integral(v.get<std::int64_t>());
               break;
            case Value::Kind::UInt64:
               integral(v.get<std::uint64_t>());
               break;
            case Value::Kind::Double:
               floating(v.get<double>());
               break;
            case Value::Kind::String:
               quoted(v.get<std::string>());
               break;
            case Value::Kind::Array: {
               character('[');
               bool first = true;
               for (const auto& element : v.get<Value::Array>()) {
                  if (!first)
                     character(',');
                  first = false;
                  value(element, depth + 1);
               }
               character(']');
               break;
            }
            case Value::Kind::Object: {
               character('{');
               bool first = true;
               for (const auto& [key, element] : v.get<Value::Object>()) {
                  if (!first)
                     character(',');
                  first = false;
                  quoted(key);
                  character(':');
                  value(element, depth + 1);
               }
               character('}');
               break;
            }
            }
         }
         std::string out_;
         JsonOptions options_;
         std::size_t nodes_{};
      };
   } // namespace

   Value parse_json(std::string_view source, JsonOptions options) { return Parser(source, options).parse(); }
   std::string to_json(const Value& value, JsonOptions options) { return Writer(options).dump(value); }
} // namespace CxxRelay
