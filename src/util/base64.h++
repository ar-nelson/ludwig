#pragma once
#include <algorithm>
#include <string>
#include <cstring>

// constexpr base64 conversion, using a FixedString wrapper type.
//
// Adapted from https://codereview.stackexchange.com/q/246089,
// with improvements from https://codereview.stackexchange.com/a/246257
//
// Unlike the rest of this project, this file is licensed CC-BY-SA 4.0,
// like the original StackExchange posts it is based on:
//
// https://creativecommons.org/licenses/by-sa/4.0/

namespace Base64 {
  template<size_t N> struct FixedString {
    char buf[N + 1] {};
    constexpr FixedString() = default;
    constexpr FixedString(char const* s) {
      std::copy(s, s + N, buf);
    }
    template <size_t S> constexpr FixedString(FixedString<S> const& other) {
      std::copy(other.buf, other.buf + std::min(S, N), buf);
    }
    auto constexpr operator==(FixedString const& other) const {
      return std::equal(buf, buf + N, other.buf);
    }
    constexpr operator char const *() const { return buf; }
    constexpr operator char *() { return buf; }
    size_t constexpr size() const { return N; }
  };

  template<size_t N>
  FixedString(char const (&)[N]) -> FixedString<N - 1>;

  // Doesn't seem like it works?
  /*
  template<FixedString string> auto constexpr decode_const() {
    size_t constexpr string_size = string.size();
    auto constexpr find_padding = [string_size]() {
      return std::distance(string.buf,
         std::find(string.buf, string.buf + string_size, '='));
    };
    FixedString<find_padding() * 3 / 4> result;
    auto constexpr convert_char = [](auto const & ch) {
      if (ch >= 'A' && ch <= 'Z')
        return ch - 65;
      else if (ch >= 'a' && ch <= 'z')
        return ch - 71;
      else if (ch >= '0')
        return ch + 4;
      else
        return ch == '-' ? 62 : 63;
    };
    for (size_t i = 0, j = 0; i < string_size; i += 4, j += 3) {
      char bytes[3] = {
        static_cast<char>(convert_char(string[i]) << 2
            | convert_char(string[i + 1]) >> 4),
        static_cast<char>(convert_char(string[i + 1]) << 4
            | convert_char(string[i + 2]) >> 2),
        static_cast<char>(convert_char(string[i + 2]) << 6
            | convert_char(string[i + 3])),
      };
      result[j] = bytes[0];
      result[j + 1] = bytes[1];
      if (string[i + 3] != '=')
        result[j + 2] = bytes[2];
    }
    return result;
  }
  */

  template<FixedString string> auto constexpr encode_const() {
    size_t constexpr string_size = string.size(),
      result_size_no_padding = (string_size * 4 + 2) / 3,
      result_size = (result_size_no_padding + 3) & (size_t)(-4),
      padding_size = result_size - result_size_no_padding;
    FixedString<(string_size + 2) / 3 * 3> constexpr string_with_padding = string;
    FixedString<result_size> result;
    auto constexpr convert_num = [](auto const & num) {
      if (num < 26)
        return static_cast<char>(num + 65);
      else if (num > 25 && num < 52)
        return static_cast<char>(num + 71);
      else if (num > 51)
        return static_cast<char>(num - 4);
      else
        return num == 62 ? '-' : '_';
    };
    for (size_t i = 0, j = 0; i < string_size; i += 3, j += 4)
    {
      /* convert every 3 bytes to 4 6 bit numbers
       * 8 * 3 = 24
       * 6 * 4 = 24
       */
      char bytes[4] = {
        static_cast<char>(string_with_padding[i] >> 2),
        static_cast<char>((string_with_padding[i] & 3) << 4
            | string_with_padding[i + 1] >> 4),
        static_cast<char>((string_with_padding[i + 1] & 15) << 2
            | string_with_padding[i + 2] >> 6),
        static_cast<char>(string_with_padding[i + 2] & 63)
      };
      std::transform(bytes, bytes + 4, result.buf + j, convert_num);
    }
    std::fill_n(result.buf + result_size_no_padding, padding_size, '=');
    return result;
  }

  auto encode(const char* data, size_t len, bool add_equals = true) -> std::string;

  static inline auto encode(const uint8_t* data, size_t len, bool add_equals = true) -> std::string {
    return encode(reinterpret_cast<const char*>(data), len, add_equals);
  }

  static inline auto encode(const std::string_view data, bool add_equals = true) -> std::string {
    return encode(data.data(), data.length(), add_equals);
  }

  static inline auto encode(const std::string data, bool add_equals = true) -> std::string {
    return encode(data.data(), data.length(), add_equals);
  }

  auto decode(const std::string_view& input, char* out, size_t out_len) -> size_t;

  auto decode(const std::string_view& input, std::string& out) -> size_t;

  auto decode(const std::string_view& input) -> std::string;

  static inline auto decode(const std::string_view& input, uint8_t* out, size_t out_len) -> size_t {
    return decode(input, reinterpret_cast<char*>(out), out_len);
  }

  static inline auto decode(const std::string& input, char* out, size_t out_len) -> size_t {
    return decode(std::string_view(input), out, out_len);
  }

  static inline auto decode(const std::string& input, uint8_t* out, size_t out_len) -> size_t {
    return decode(input, reinterpret_cast<char*>(out), out_len);
  }

  static inline auto decode(const std::string& input, std::string& out) -> size_t {
    return decode(std::string_view(input), out);
  }

  static inline auto decode(const std::string& input) -> std::string {
    return decode(std::string_view(input));
  }
}
