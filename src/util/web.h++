#pragma once
#include "util/common.h++"
#include "models/db.h++"
#include <uWebSockets/App.h>

namespace Ludwig {
  static constexpr std::string_view ESCAPED = "<>'\"&",
    TYPE_HTML = "text/html; charset=utf-8",
    TYPE_CSS = "text/css; charset=utf-8",
    TYPE_JS = "text/javascript; charset=utf-8",
    TYPE_SVG = "image/svg+xml; charset=utf-8",
    TYPE_WEBP = "image/webp",
    TYPE_FORM = "application/x-www-form-urlencoded";

  static constexpr auto http_status(uint16_t code) -> std::string_view {
    switch (code) {
      case 200: return "200 OK";
      case 201: return "201 Created";
      case 202: return "202 Accepted";
      case 204: return "204 No Content";
      case 301: return "301 Moved Permanently";
      case 302: return "302 Found";
      case 303: return "303 See Other";
      case 304: return "304 Not Modified";
      case 307: return "307 Temporary Redirect";
      case 308: return "308 Permanent Redirect";
      case 400: return "400 Bad Request";
      case 401: return "401 Unauthorized";
      case 403: return "403 Forbidden";
      case 404: return "404 Not Found";
      case 405: return "405 Method Not Allowed";
      case 406: return "406 Not Acceptable";
      case 408: return "408 Request Timeout";
      case 409: return "409 Conflict";
      case 410: return "410 Gone";
      case 413: return "413 Payload Too Large";
      case 415: return "413 Unsupported Media Type";
      case 418: return "418 I'm a teapot";
      case 422: return "422 Unprocessable Entity";
      case 429: return "429 Too Many Requests";
      case 451: return "451 Unavailable For Legal Reasons";
      case 500: return "500 Internal Server Error";
      case 501: return "501 Not Implemented";
      case 503: return "503 Service Unavailable";
      default: return "500 Internal Server Error";
    }
  }

  struct ApiError : public std::runtime_error {
    uint16_t http_status;
    std::string message, internal_message;
    ApiError(std::string message, uint16_t http_status = 500, std::string internal_message = { nullptr, 0 })
      : std::runtime_error(internal_message.data() ? fmt::format("{} - {}", message, internal_message) : std::string(message)),
        http_status(http_status), message(message), internal_message(internal_message) {}
  };

  static bool behind_reverse_proxy = true;

  static inline auto get_ip(uWS::HttpResponse<false>* rsp, uWS::HttpRequest* req) -> std::string_view {
    // Hacky way to deal with x-forwarded-for:
    // If we're behind a reverse proxy, then every request will have x-forwarded-for.
    // If we EVER see a request without x-forwarded-for, ignore it from now on.
    if (behind_reverse_proxy) {
      auto forwarded_for = req->getHeader("x-forwarded-for");
      if (!forwarded_for.empty()) return forwarded_for.substr(0, forwarded_for.find(','));
      behind_reverse_proxy = false;
    }
    return rsp->getRemoteAddressAsText();
  };

  static inline auto get_ip(uWS::HttpResponse<true>* rsp, uWS::HttpRequest*) -> std::string_view {
    // Assume that SSL connections will never be behind a reverse proxy
    return rsp->getRemoteAddressAsText();
  };

  static inline auto get_query_param(std::string_view query, std::string_view key) -> std::string_view {
    return uWS::getDecodedQueryValue(key, query);
  }

  static inline auto get_query_param(uWS::HttpRequest* req, std::string_view key) -> std::string_view {
    return req->getQuery(key);
  }

  template <typename T>
  struct QueryString {
    T query;
    QueryString(const T& query) : query(query) {}

    auto required_hex_id(std::string_view key) -> uint64_t {
      try {
        return std::stoull(std::string(get_query_param(query, key)), nullptr, 16);
      } catch (...) {
        throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      }
    }
    auto required_int(std::string_view key) -> int {
      try {
        return std::stoi(std::string(get_query_param(query, key)));
      } catch (...) {
        throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      }
    }
    auto required_string(std::string_view key) -> std::string_view {
      auto s = get_query_param(query, key);
      if (s.empty()) throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      return s;
    }
    auto required_vote(std::string_view key) -> Vote {
      const auto vote_str = get_query_param(query, key);
      if (vote_str == "1") return Vote::Upvote;
      else if (vote_str == "-1") return Vote::Downvote;
      else if (vote_str == "0") return Vote::NoVote;
      else throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
    }
    auto optional_id(std::string_view key) -> uint64_t {
      auto s = get_query_param(query, key);
      if (s.empty()) return 0;
      try {
        return std::stoull(std::string(s), nullptr, 16);
      } catch (...) {
        throw ApiError(fmt::format("Invalid '{}' parameter", key), 400);
      }
    }
    auto string(std::string_view key) -> std::string_view {
      return get_query_param(query, key);
    }
    auto optional_string(std::string_view key) -> std::optional<std::string_view> {
      auto s = get_query_param(query, key);
      if (s.empty()) return {};
      return s;
    }
    auto optional_uint(std::string_view key) -> std::optional<uint64_t> {
      auto s = get_query_param(query, key);
      if (s.empty()) return {};
      try {
        return std::stoul(std::string(s));
      } catch (...) {
        throw ApiError(fmt::format("Invalid '{}' parameter", key), 400);
      }
    }
    auto optional_bool(std::string_view key) -> bool {
      const auto decoded = get_query_param(query, key);
      return !(decoded.empty() || decoded == "0" || decoded == "false");
    }
  };

  static inline auto hex_id_param(uWS::HttpRequest* req, uint16_t param) {
    const auto str = req->getParameter(param);
    uint64_t id;
    const auto res = std::from_chars(str.begin(), str.end(), id, 16);
    if (res.ec != std::errc{} || res.ptr != str.data() + str.length()) {
      throw ApiError(fmt::format("Invalid hexadecimal ID: ", str), 400);
    }
    return id;
  }

  struct Escape {
    std::string_view str;
    Escape(std::string_view str) : str(str) {}
    Escape(const flatbuffers::String* fbs) : str(fbs ? fbs->string_view() : "") {}
  };
  struct Suffixed {
    int64_t n;
  };
  struct RelativeTime {
    Timestamp t;
  };
}

namespace fmt {
  template <> struct formatter<Ludwig::Escape> : public Ludwig::CustomFormatter {
    template <typename FormatContext>
    auto format(Ludwig::Escape e, FormatContext& ctx) const {
      size_t start = 0;
      for (
        size_t i = e.str.find_first_of(Ludwig::ESCAPED);
        i != std::string_view::npos;
        start = i + 1, i = e.str.find_first_of(Ludwig::ESCAPED, start)
      ) {
        if (i > start) std::copy(e.str.begin() + start, e.str.begin() + i, ctx.out());
        switch (e.str[i]) {
          case '<': format_to(ctx.out(), "&lt;"_cf); break;
          case '>': format_to(ctx.out(), "&gt;"_cf); break;
          case '\'': format_to(ctx.out(), "&apos;"_cf); break;
          case '"': format_to(ctx.out(), "&quot;"_cf); break;
          case '&': format_to(ctx.out(), "&amp;"_cf); break;
        }
      }
      return std::copy(e.str.begin() + start, e.str.end(), ctx.out());
    }
  };

  template <> struct formatter<Ludwig::Suffixed> : public Ludwig::CustomFormatter {
    // Adapted from https://programming.guide/java/formatting-byte-size-to-human-readable-format.html
    template <typename FormatContext>
    auto format(Ludwig::Suffixed x, FormatContext &ctx) const {
      static constexpr auto SUFFIXES = "KMBTqQ";
      auto n = x.n;
      if (-1000 < n && n < 1000)
        return format_to(ctx.out(), "{:d}"_cf, n);
      uint8_t i = 0;
      while (n <= -999'950 || n >= 999'950) {
        n /= 1000;
        i++;
      }
      return format_to(ctx.out(), "{:.3g}{:c}"_cf, (double)n / 1000.0, SUFFIXES[i]);
      // SUFFIXES[i] can never overflow, max 64-bit int is ~18 quintillion (Q)
    }
  };

  template <> struct formatter<Ludwig::RelativeTime> : public Ludwig::CustomFormatter {
    template <typename FormatContext>
    static auto write(FormatContext &ctx, const char s[]) {
      return std::copy(s, s + std::char_traits<char>::length(s), ctx.out());
    }

    template <typename FormatContext>
    auto format(Ludwig::RelativeTime x, FormatContext &ctx) const {
      using namespace std::chrono;
      const auto now = Ludwig::now_t();
      if (x.t > now)
        return write(ctx, "in the future");
      const auto diff = now - x.t;
      if (diff < 1min)
        return write(ctx, "just now");
      if (diff < 2min)
        return write(ctx, "1 minute ago");
      if (diff < 1h)
        return format_to(ctx.out(), "{:d} minutes ago"_cf, duration_cast<minutes>(diff).count());
      if (diff < 2h)
        return write(ctx, "1 hour ago");
      if (diff < days{1})
        return format_to(ctx.out(), "{:d} hours ago"_cf, duration_cast<hours>(diff).count());
      if (diff < days{2})
        return write(ctx, "1 day ago");
      if (diff < weeks{1})
        return format_to(ctx.out(), "{:d} days ago"_cf, duration_cast<days>(diff).count());
      if (diff < weeks{2})
        return write(ctx, "1 week ago");
      if (diff < months{1})
        return format_to(ctx.out(), "{:d} weeks ago"_cf, duration_cast<weeks>(diff).count());
      if (diff < months{2})
        return write(ctx, "1 month ago");
      if (diff < years{1})
        return format_to(ctx.out(), "{:d} months ago"_cf, duration_cast<months>(diff).count());
      if (diff < years{2})
        return write(ctx, "1 year ago");
      return format_to(ctx.out(), "{:d} years ago"_cf, duration_cast<years>(diff).count());
    }
  };
}

namespace Ludwig {
  static inline auto escape_html(std::string_view str) -> std::string {
    using fmt::operator""_cf;
    return fmt::format("{}"_cf, Escape(str));
  }
}

#define ICON(NAME) \
  R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#)" NAME R"("></svg>)"
#define HTML_FIELD(ID, LABEL, TYPE, EXTRA) \
  "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"" TYPE "\" name=\"" ID "\" id=\"" ID "\"" EXTRA "></label>"
#define HTML_CHECKBOX(ID, LABEL, EXTRA) \
  "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"checkbox\" class=\"a11y\" name=\"" ID "\" id=\"" ID "\"" EXTRA "><div class=\"toggle-switch\"></div></label>"
#define HTML_TEXTAREA(ID, LABEL, EXTRA, CONTENT) \
  "<label for=\"" ID "\"><span>" LABEL "</span><div><textarea name=\"" ID "\" id=\"" ID "\"" EXTRA ">" CONTENT \
  R"(</textarea><small><a href="https://www.markdownguide.org/cheat-sheet/" rel="nofollow" target="_blank">Markdown</a> formatting is supported.</small></div></label>)"