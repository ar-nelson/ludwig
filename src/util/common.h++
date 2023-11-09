#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <chrono>
#include <functional>
#include <limits>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <libxml/parser.h>

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
    return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count()
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
}
