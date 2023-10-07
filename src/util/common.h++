#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <chrono>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

namespace Ludwig {
  constexpr uint64_t ID_MAX = std::numeric_limits<uint64_t>::max();

  template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
  template<class... Ts> overload(Ts...) -> overload<Ts...>;

  template<typename T> static inline auto to_ascii_lowercase(T in) -> std::string {
    std::string out;
    std::transform(in.begin(), in.end(), std::back_inserter(out), [](auto c){ return std::tolower(c); });
    return out;
  }

  static inline auto now_s() -> uint64_t {
    return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count()
    );
  }
}
