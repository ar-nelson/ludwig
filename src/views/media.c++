#include "media.h++"
#include "util/web.h++"

using std::function, std::runtime_error, std::shared_ptr, std::stoull, std::string;

namespace Ludwig {
  template <bool SSL> static inline auto webp_route(
    uWS::HttpRequest* req,
    uWS::HttpResponse<SSL>* rsp,
    function<void (ThumbnailCache::Callback)> fn
  ) -> void {
    // fn is called asynchronously, and async + uWebSockets is a quagmire of
    // segfaults. A few things I learned while debugging this function:
    //
    // - req is stack allocated. It's gone by the time fn is called.
    // - if onAborted is called then rsp is also gone, so we need a flag.
    // - …and that flag must be on the heap, for obvious-in-hindsight reasons.
    //
    auto abort_flag = std::make_shared<bool>(false);
    rsp->onAborted([abort_flag]{
      *abort_flag = true;
      spdlog::debug("HTTP session aborted");
    });
    const string if_none_match(req->getHeader("if-none-match"));

    try {
      fn([rsp, abort_flag, if_none_match](ThumbnailCache::Image img) {
        if (*abort_flag) return;
        if (!*img) {
          rsp->cork([rsp]{
            rsp->writeStatus(http_status(404))
              ->writeHeader("Content-Type", "text/plain")
              ->end("404 Not Found");
          });
          return;
        }
        const auto& [data, hash] = **img;
        const auto hash_hex = fmt::format("{:016x}", hash);
        if (if_none_match == hash_hex) {
          rsp->cork([rsp]{
            rsp->writeStatus(http_status(304))->end();
          });
          return;
        }
        rsp->cork([&]{
          rsp->writeHeader("Content-Type", TYPE_WEBP)
            ->writeHeader("Etag", hash_hex)
            ->end(data);
        });
      });
    } catch (const runtime_error& e) {
      spdlog::warn("Error in WebP thumbnail handler: {}", e.what());
      rsp->writeStatus(http_status(500))
        ->writeHeader("Content-Type", "text/plain")
        ->end("500 Internal Server Error");
    }
  }

  template <bool SSL> auto media_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<RemoteMediaController> controller
  ) -> void {
    app.get("/media/user/:name/avatar.webp", [controller](auto* rsp, auto* req) {
      webp_route(req, rsp, [&](auto cb) { controller->user_avatar(req->getParameter(0), cb); });
    });
    app.get("/media/user/:name/banner.webp", [controller](auto* rsp, auto* req) {
      webp_route(req, rsp, [&](auto cb) { controller->user_banner(req->getParameter(0), cb); });
    });
    app.get("/media/board/:name/icon.webp", [controller](auto* rsp, auto* req) {
      webp_route(req, rsp, [&](auto cb) { controller->board_icon(req->getParameter(0), cb); });
    });
    app.get("/media/board/:name/banner.webp", [controller](auto* rsp, auto* req) {
      webp_route(req, rsp, [&](auto cb) { controller->board_banner(req->getParameter(0), cb); });
    });
    app.get("/media/thread/:id/thumbnail.webp", [controller](auto* rsp, auto* req) {
      uint64_t id;
      try { id = stoull(string(req->getParameter(0))); }
      catch (...) {
        rsp->writeStatus(http_status(404))
          ->writeHeader("Content-Type", "text/plain")
          ->end("404 Not Found");
        return;
      }
      webp_route(req, rsp, [&](auto cb) { controller->thread_preview(id, cb); });
    });
  }

  template auto media_routes<true>(
    uWS::TemplatedApp<true>& app,
    shared_ptr<RemoteMediaController> controller
  ) -> void;

  template auto media_routes<false>(
    uWS::TemplatedApp<false>& app,
    shared_ptr<RemoteMediaController> controller
  ) -> void;
}
