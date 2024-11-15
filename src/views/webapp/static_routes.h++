#pragma once
#include "webapp_common.h++"
#include "static/default-theme.min.css.h++"
#include "static/feather-sprite.svg.h++"
#include "static/htmx.min.js.h++"
#include "static/ludwig.js.h++"
#include "static/twemoji-piano.ico.h++"
#include <xxhash.h>

namespace Ludwig {

template <bool SSL>
void serve_static(
  uWS::TemplatedApp<SSL>& app,
  std::string path,
  std::string_view mimetype,
  std::string_view src
) noexcept {
  using fmt::operator""_cf;
  const auto hash = format("\"{:016x}\""_cf, XXH3_64bits(src.data(), src.length()));
  app.get(path, [src, mimetype, hash](auto* res, auto* req) {
    if (req->getHeader("if-none-match") == hash) {
      res->writeStatus(http_status(304))->end();
    } else {
      res->writeHeader("Content-Type", mimetype)
        ->writeHeader("Etag", hash)
        ->end(src);
    }
  });
}

template <bool SSL>
void define_static_routes(uWS::TemplatedApp<SSL>& app) {
  serve_static(app, "/favicon.ico", "image/vnd.microsoft.icon", twemoji_piano_ico_str());
  serve_static(app, "/static/default-theme.css", TYPE_CSS, default_theme_min_css_str());
  serve_static(app, "/static/htmx.min.js", TYPE_JS, htmx_min_js_str());
  serve_static(app, "/static/ludwig.js", TYPE_JS, ludwig_js_str());
  serve_static(app, "/static/feather-sprite.svg", TYPE_SVG, feather_sprite_svg_str());
}

}