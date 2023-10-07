#pragma once
#include <stdint.h>
#include <stdlib.h>
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

  enum Vote {
    Downvote = -1,
    NoVote = 0,
    Upvote = 1
  };

  template<typename T> static inline auto to_ascii_lowercase(T in) -> std::string {
    std::string out;
    std::transform(in.begin(), in.end(), std::back_inserter(out), [](auto c){ return std::tolower(c); });
    return out;
  }
}
