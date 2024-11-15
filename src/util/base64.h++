#pragma once
#include <stdint.h>
#include <string>
#include <cstring>

namespace Base64 {

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
