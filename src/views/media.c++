#include "media.h++"
#include "util/web.h++"

using std::function, std::shared_ptr, std::stoull, std::string;

namespace Ludwig {
  template <bool SSL> static inline auto webp_route(
    uWS::HttpRequest* req,
    uWS::HttpResponse<SSL>* rsp,
    uWS::MoveOnlyFunction<void (function<void ()>)> wrap,
    function<void (ThumbnailCache::Callback)> fn
  ) -> void {
    const string if_none_match(req->getHeader("if-none-match"));
    fn([rsp, wrap = std::move(wrap), if_none_match](ThumbnailCache::Image img) mutable {
      wrap([&]{
        if (!*img) throw ApiError("No thumbnail available", 404);
        const auto& [data, hash] = **img;
        const auto hash_hex = fmt::format("{:016x}", hash);
        if (if_none_match == hash_hex) {
          rsp->writeStatus(http_status(304))->end();
        } else {
          rsp->writeHeader("Content-Type", TYPE_WEBP)
            ->writeHeader("Etag", hash_hex)
            ->end(data);
        }
      });
    });
  }

  template <bool SSL> auto media_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<RemoteMediaController> controller
  ) -> void {
    Router(app)
      .get_async("/media/user/:name/avatar.webp", [controller](auto* rsp, auto* req, auto, auto wrap) {
        webp_route(req, rsp, std::move(wrap), [&](auto cb) { controller->user_avatar(req->getParameter(0), std::move(cb)); });
      })
      .get_async("/media/user/:name/banner.webp", [controller](auto* rsp, auto* req, auto, auto wrap) {
        webp_route(req, rsp, std::move(wrap), [&](auto cb) { controller->user_banner(req->getParameter(0), std::move(cb)); });
      })
      .get_async("/media/board/:name/icon.webp", [controller](auto* rsp, auto* req, auto, auto wrap) {
        webp_route(req, rsp, std::move(wrap), [&](auto cb) { controller->board_icon(req->getParameter(0), std::move(cb)); });
      })
      .get_async("/media/board/:name/banner.webp", [controller](auto* rsp, auto* req, auto, auto wrap) {
        webp_route(req, rsp, std::move(wrap), [&](auto cb) { controller->board_banner(req->getParameter(0), std::move(cb)); });
      })
      .get_async("/media/thread/:id/thumbnail.webp", [controller](auto* rsp, auto* req, auto, auto wrap) {
        uint64_t id;
        try { id = stoull(string(req->getParameter(0))); }
        catch (...) { throw ApiError("Invalid thread ID", 404); }
        webp_route(req, rsp, std::move(wrap), [&](auto cb) { controller->thread_preview(id, std::move(cb)); });
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
