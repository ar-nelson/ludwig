#pragma once
#include "iter.h++"

namespace Ludwig {

struct PageCursor {
  bool exists = false;
  uint64_t k, v;

  PageCursor() : exists(false) {}
  explicit PageCursor(uint64_t k) : exists(true), k(k) {}
  PageCursor(uint64_t k, uint64_t v) : exists(true), k(k), v(v) {}
  PageCursor(double k, uint64_t v) : exists(true), k(*reinterpret_cast<uint64_t*>(&k)), v(v) {}
  PageCursor(std::string_view str) {
    static const std::regex re(R"(^([0-9a-f]+)(?:_([0-9a-f]+))?$)", std::regex::icase);
    if (str.empty()) return;
    std::match_results<std::string_view::const_iterator> match;
    if (std::regex_match(str.begin(), str.end(), match, re)) {
      exists = true;
      k = std::stoull(std::string(match[1].str()), nullptr, 16);
      if (match[2].matched) v = std::stoull(std::string(match[2].str()), nullptr, 16);
    } else {
      using namespace fmt;
      throw ApiError(format("Invalid cursor: {}"_cf, str), 400);
    }
  }

  operator bool() const noexcept { return exists; }
  auto to_string() const noexcept -> std::string {
    using namespace fmt;
    if (!exists) return "";
    if (!v) return format("{:x}"_cf, k);
    return format("{:x}_{:x}"_cf, k, v);
  }

  using OptKV = std::optional<std::pair<Cursor, uint64_t>>;

  auto rank_k() -> double {
    return exists ? *reinterpret_cast<double*>(&k) : INFINITY;
  }
  auto next_cursor_desc() -> OptKV {
    if (!exists) return {};
    return {{Cursor(k), v ? v - 1 : v}};
  }
  auto next_cursor_asc() -> OptKV {
    if (!exists) return {};
    return {{Cursor(k), v ? v + 1 : ID_MAX}};
  }
  auto next_cursor_desc(uint64_t prefix) -> OptKV {
    if (!exists) return {};
    return {{Cursor(prefix, k), v ? v - 1 : v}};
  }
  auto next_cursor_asc(uint64_t prefix) -> OptKV {
    if (!exists) return {};
    return {{Cursor(prefix, k), v ? v + 1 : ID_MAX}};
  }

  auto reset() {
    exists = false;
  }
  auto set(uint64_t _k) -> void {
    exists = true;
    k = _k;
  }
  auto set(uint64_t _k, uint64_t _v) -> void {
    exists = true;
    k = _k;
    v = _v;
  }
};

}