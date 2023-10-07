#pragma once
#include "util/common.h++"
#include <regex>
#include <sstream>
#include <uWebSockets/App.h>
#include <flatbuffers/string.h>

namespace Ludwig {
  static constexpr std::string_view ESCAPED = "<>'\"&",
    TYPE_HTML = "text/html; charset=utf-8",
    TYPE_CSS = "text/css; charset=utf-8",
    TYPE_JS = "text/javascript; charset=utf-8",
    TYPE_SVG = "image/svg+xml; charset=utf-8",
    TYPE_WEBP = "image/webp";

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

  struct Escape {
    std::string_view str;
    Escape(std::string_view str) : str(str) {}
    Escape(const flatbuffers::String* fbs) : str(fbs ? fbs->string_view() : "") {}
  };

  template <bool SSL> static inline auto operator<<(uWS::HttpResponse<SSL>& lhs, const std::string_view rhs) -> uWS::HttpResponse<SSL>& {
    lhs.write(rhs);
    return lhs;
  }

  template <bool SSL> static inline auto operator<<(uWS::HttpResponse<SSL>& lhs, int64_t rhs) -> uWS::HttpResponse<SSL>& {
    lhs.write(fmt::format("{}", rhs));
    return lhs;
  }

  template <bool SSL> static inline auto operator<<(uWS::HttpResponse<SSL>& lhs, uint64_t rhs) -> uWS::HttpResponse<SSL>& {
    lhs.write(fmt::format("{}", rhs));
    return lhs;
  }
}

namespace fmt {
  template <> struct formatter<Ludwig::Escape> {
    constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator {
      auto it = ctx.begin();
      if (it != ctx.end()) detail::throw_format_error("invalid format");
      return it;
    }

    auto format(Ludwig::Escape e, format_context& ctx) const {
      size_t start = 0;
      for (
        size_t i = e.str.find_first_of(Ludwig::ESCAPED);
        i != std::string_view::npos;
        start = i + 1, i = e.str.find_first_of(Ludwig::ESCAPED, start)
      ) {
        if (i > start) std::copy(e.str.begin() + start, e.str.begin() + i, ctx.out());
        switch (e.str[i]) {
          case '<':
            format_to(ctx.out(), "&lt;");
            break;
          case '>':
            format_to(ctx.out(), "&gt;");
            break;
          case '\'':
            format_to(ctx.out(), "&apos;");
            break;
          case '"':
            format_to(ctx.out(), "&quot;");
            break;
          case '&':
            format_to(ctx.out(), "&amp;");
            break;
        }
      }
      return std::copy(e.str.begin() + start, e.str.end(), ctx.out());
    }
  };
}

namespace Ludwig {
  static inline auto escape_html(std::string_view str) -> std::string {
    return fmt::format("{}", Escape(str));
  }
}
