#include "webapp_routes.h++"
#include <spdlog/fmt/fmt.h>
#include "xxhash.h"
#include "generated/default-theme.css.h"
#include "generated/htmx.min.js.h"

namespace Ludwig {
  static constexpr std::string_view HTML_HEADER = R"(<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1,shrink-to-fit=no">
    <title>Ludwig Demo</title>
    <link rel="stylesheet" href="/static/default-theme.css">
  </head>
  <body>
)",
  HTML_FOOTER = R"(
  </body>
</html>
)",
  ESCAPED = "<>'\"&";

  static inline auto hexstring(uint64_t n, bool padded = false) -> std::string {
    std::string s;
    if (padded) fmt::format_to(std::back_inserter(s), "{:016x}", n);
    else fmt::format_to(std::back_inserter(s), "{:x}", n);
    return s;
  }

  template <bool SSL> auto serve_static(
    uWS::TemplatedApp<SSL>& app,
    std::string filename,
    std::string mimetype,
    const unsigned char* src,
    size_t len
  ) -> void {
    const auto hash = hexstring(XXH3_64bits(src, len), true);
    app.get("/static/" + filename, [src, len, mimetype, hash](auto* res, auto* req) {
      if (req->getHeader("if-none-match") == hash) {
        res->writeStatus("304 Not Modified")->end();
      } else {
        res->writeHeader("Content-Type", mimetype)
          ->writeHeader("Etag", hash)
          ->end(std::string_view(reinterpret_cast<const char*>(src), len));
      }
    });
  }

  template <bool SSL> auto write_escaped(uWS::HttpResponse<SSL>* res, std::string_view str) -> void {
    size_t start = 0;
    for (size_t i = str.find_first_of(ESCAPED); i != std::string_view::npos; start = i + 1, i = str.find_first_of(ESCAPED, start)) {
      if (i > start) res->write(str.substr(start, i - start));
      switch (str[i]) {
        case '<':
          res->write("&lt;");
          break;
        case '>':
          res->write("&gt;");
          break;
        case '\'':
          res->write("&apos;");
          break;
        case '"':
          res->write("&quot;");
          break;
        case '&':
          res->write("&amp;");
          break;
      }
    }
    if (start < str.length()) res->write(str.substr(start));
  }

  template <bool SSL> auto webapp_routes(
    uWS::TemplatedApp<SSL>& app,
    Controller& controller
  ) -> void {
    app.get("/", [&controller](uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req) {
      res->writeHeader("Content-Type", "text/html; charset=utf-8")
        ->write(HTML_HEADER);
      write_escaped(res, "<Hello, world!>");
      res->end(HTML_FOOTER);
    });
    serve_static(app, "default-theme.css", "text/css; charset=utf-8", default_theme_css, default_theme_css_len);
    serve_static(app, "htmx.min.js", "text/javascript; charset=utf-8", htmx_min_js, htmx_min_js_len);
  }

  template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    Controller& controller
  ) -> void;

  template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    Controller& controller
  ) -> void;
}
