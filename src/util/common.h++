#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
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
#include <uWebSockets/MoveOnlyFunction.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <flatbuffers/string.h>

namespace Ludwig {
  constexpr std::string_view VERSION = "0.0.1";
  constexpr std::string_view FIRST_RUN_ADMIN_USERNAME = "admin";
  constexpr uint64_t ID_MIN_USER = 0x10;
  constexpr uint64_t ID_MAX = std::numeric_limits<uint64_t>::max();
  constexpr size_t MiB = 1024 * 1024;

  template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
  template<class... Ts> overload(Ts...) -> overload<Ts...>;

  template <typename T> using OptRef = std::optional<std::reference_wrapper<const T>>;

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

  struct SecretString {
    std::string data;
    SecretString(std::string&& from) { std::swap(data, from); }
    SecretString(std::string_view from) : data(from) {
      OPENSSL_cleanse((char*)from.data(), from.length());
    };
    // Special case: don't OPENSSL_cleanse string literals
    SecretString(const char string_literal[]) : data(string_literal) {};

    SecretString(const SecretString&) = delete;
    SecretString& operator=(const SecretString&) = delete;
    SecretString(SecretString&& from) { std::swap(data, from.data); }
    SecretString& operator=(SecretString&& from) { std::swap(data, from.data); return *this; }
    ~SecretString() { if (data.length()) OPENSSL_cleanse(data.data(), data.capacity()); }

    operator std::string_view() { return data; }
  };

  // Based on https://stackoverflow.com/a/53526139/548027
  struct Url {
  private:
    static constexpr inline char
      SCHEME_REGEX[]   = "(?:([a-z0-9-]+)://)", // mandatory, match protocol before the ://
      USER_REGEX[]     = "(?:([^@/:\\s]+)@)?",  // match anything other than @ / : or whitespace before the ending @
      HOST_REGEX[]     = "([^@/:\\s]+)",        // mandatory. match anything other than @ / : or whitespace
      PORT_REGEX[]     = "(?::([0-9]{1,5}))?",  // after the : match 1 to 5 digits
      PATH_REGEX[]     = "(/[^:#?\\s]*)?",      // after the / match anything other than : # ? or whitespace
      QUERY_REGEX[]    = "(\\?(?:(?:[^?;&#=]+(?:=[^?;&#=]*)?)(?:[;|&](?:[^?;&#=]+(?:=[^?;&#=]*)?))*))?", // after the ? match any number of x=y pairs, seperated by & or ;
      FRAGMENT_REGEX[] = "(?:#([^#\\s]*))?";    // after the # match anything other than # or whitespace
    static const inline auto regex = std::regex(std::string("^")
      + SCHEME_REGEX + USER_REGEX
      + HOST_REGEX + PORT_REGEX
      + PATH_REGEX + QUERY_REGEX
      + FRAGMENT_REGEX + "$",
      std::regex::icase | std::regex::optimize
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

  // Common base class for custom formatters
  struct CustomFormatter {
    constexpr auto parse(fmt::format_parse_context& ctx) -> fmt::format_parse_context::iterator {
      auto it = ctx.begin();
      if (it != ctx.end()) fmt::detail::throw_format_error("invalid format");
      return it;
    }
  };

  static inline auto opt_c_str(const flatbuffers::String* s) -> std::optional<const char*> {
    if (s) return s->c_str();
    return {};
  }

  static inline auto opt_str(const flatbuffers::String* s) -> std::optional<std::string> {
    if (s) return s->str();
    return {};
  }

  static inline auto opt_sv(const flatbuffers::String* s) -> std::optional<std::string_view> {
    if (s) return s->string_view();
    return {};
  }

  static inline auto generate_password(uint8_t length = 8) -> SecretString {
    // 64 characters, 6 bits per char
    static constexpr char CHARS[65] = "23456789abcdefghjkmnopqrstvwxyzABCDEFGHJKLMNPQRSTVWXYZ.+-=?!@%&*";
    uint8_t bytes[256];
    RAND_bytes(bytes, length);
    std::string password;
    password.reserve(length);
    for (uint8_t i = 0; i < length; i++) password.push_back(CHARS[bytes[i] >> 2]);
    return SecretString(password);
  }

  template<typename T, size_t Size> struct ConstArray {
    T arr[Size];

    constexpr ConstArray(T x) : arr() {
      for (size_t i = 0; i < Size; i++) arr[i] = x;
    }
  };

  struct Defer {
    uWS::MoveOnlyFunction<void ()> fn;
    ~Defer() { fn(); }
  };
}
