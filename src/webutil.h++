#pragma once
#include <regex>
#include <string>
#include <string_view>
#include <spdlog/fmt/fmt.h>
#include <uWebSockets/App.h>

namespace Ludwig {
  static constexpr std::string_view ESCAPED = "<>'\"&";

  static constexpr auto http_status(uint16_t code) -> std::string {
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
      default: return std::to_string(code);
    }
  }

  struct Escape {
    std::string_view str;
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

  template <typename T> static inline auto operator<<(T& lhs, Escape rhs) -> T& {
    size_t start = 0;
    for (
      size_t i = rhs.str.find_first_of(ESCAPED);
      i != std::string_view::npos;
      start = i + 1, i = rhs.str.find_first_of(ESCAPED, start)
    ) {
      if (i > start) lhs << rhs.str.substr(start, i - start);
      switch (rhs.str[i]) {
        case '<':
          lhs << "&lt;";
          break;
        case '>':
          lhs << "&gt;";
          break;
        case '\'':
          lhs << "&apos;";
          break;
        case '"':
          lhs << "&quot;";
          break;
        case '&':
          lhs << "&amp;";
          break;
      }
    }
    if (start < rhs.str.length()) lhs << rhs.str.substr(start);
    return lhs;
  }

  static inline auto escape_html(std::string_view str) -> std::string {
    std::ostringstream stream;
    stream << Escape{str};
    return stream.str();
  }
}
