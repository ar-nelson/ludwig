#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <functional>
#include <limits>
#include <list>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <libxml/parser.h>
#include <asio.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <uWebSockets/MoveOnlyFunction.h>

namespace Ludwig {
  constexpr uint64_t ID_MAX = std::numeric_limits<uint64_t>::max();

  template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
  template<class... Ts> overload(Ts...) -> overload<Ts...>;

  template<typename T> static inline auto to_ascii_lowercase(T in) -> std::string {
    std::string out;
    out.reserve(in.size());
    std::transform(in.begin(), in.end(), std::back_inserter(out), [](auto c){ return std::tolower(c); });
    return out;
  }

  static inline auto now_s() -> uint64_t {
    using namespace std::chrono;
    return static_cast<uint64_t>(
      duration_cast<seconds>(system_clock::now().time_since_epoch()).count()
    );
  }

  // Based on https://stackoverflow.com/a/53526139/548027
  struct Url {
  private:
    static const inline char
      *SCHEME_REGEX   = "(?:(\\w+)://)",      // mandatory, match protocol before the ://
      *USER_REGEX     = "(?:([^@/:\\s]+)@)?", // match anything other than @ / : or whitespace before the ending @
      *HOST_REGEX     = "([^@/:\\s]+)",       // mandatory. match anything other than @ / : or whitespace
      *PORT_REGEX     = "(?::([0-9]{1,5}))?", // after the : match 1 to 5 digits
      *PATH_REGEX     = "(/[^:#?\\s]*)?",     // after the / match anything other than : # ? or whitespace
      *QUERY_REGEX    = "(\\?(?:(?:[^?;&#=]+(?:=[^?;&#=]*)?)(?:[;|&](?:[^?;&#=]+(?:=[^?;&#=]*)?))*))?", // after the ? match any number of x=y pairs, seperated by & or ;
      *FRAGMENT_REGEX = "(?:#([^#\\s]*))?";   // after the # match anything other than # or whitespace
    static const inline auto regex = std::regex(std::string("^")
      + SCHEME_REGEX + USER_REGEX
      + HOST_REGEX + PORT_REGEX
      + PATH_REGEX + QUERY_REGEX
      + FRAGMENT_REGEX + "$"
    );
  public:
    std::string scheme, user, host, port, path, query, fragment;

    static auto parse(std::string str) noexcept -> std::optional<Url> {
      std::smatch match;
      if (!std::regex_match(str, match, regex)) return {};
      return {{
        match[1].str(), match[2].str(), match[3].str(), match[4].str(),
        match[5].str(), match[6].str(), match[7].str()
      }};
    }

    inline auto is_http_s() const noexcept -> bool {
      return scheme == "http" || scheme == "https";
    }

    inline auto to_string() const noexcept -> std::string {
      return fmt::format("{}://{}{}{}{}{}{}{}{}",
        scheme,
        user.empty() ? "" : user,
        user.empty() ? "" : "@",
        host,
        port.empty() ? "" : ":",
        port.empty() ? "" : port,
        path,
        query,
        fragment.empty() ? "" : "#",
        fragment.empty() ? "" : fragment
      );
    }
  };

  template <typename T> using Async = asio::awaitable<T, asio::io_context::executor_type>;
  template <typename ...Ts> using Chan = asio::experimental::channel<asio::io_context::executor_type, void(asio::error_code, Ts...)>;
  template <typename ...Ts> using ConcurrentChan = asio::experimental::concurrent_channel<asio::io_context::executor_type, void(asio::error_code, Ts...)>;

  template <typename T = std::monostate> class CacheChan {
  private:
    ConcurrentChan<T> chan;
  public:
    CacheChan(asio::io_context& io) : chan(io.get_executor()) {}

    inline auto get() -> Async<T> {
      auto t = co_await chan.async_receive(asio::deferred);
      chan.async_send({}, t, asio::detached);
      co_return t;
    }

    inline auto set(T&& new_value) -> void {
      chan.async_send({}, std::forward<T>(new_value), asio::detached);
    }
  };
}
