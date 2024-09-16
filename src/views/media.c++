#include "media.h++"
#include "util/web.h++"

using std::shared_ptr, std::string;
using namespace uWS;

namespace Ludwig {
  template <bool SSL> static inline auto webp_route(
    HttpRequest* req,
    HttpResponse<SSL>* rsp,
    MoveOnlyFunction<void (MoveOnlyFunction<void ()>&&)>&& wrap,
    MoveOnlyFunction<void (ThumbnailCache::Callback)>&& fn
  ) -> void {
    const string if_none_match(req->getHeader("if-none-match"));
    fn([rsp, wrap = std::move(wrap), if_none_match](ImageRef img) mutable {
      wrap([rsp, img, if_none_match]{
        if (!img) throw ApiError("No thumbnail available", 404);
        const auto hash_hex = fmt::format(R"("{:016x}")", img.hash());
        if (if_none_match == hash_hex) {
          rsp->writeStatus(http_status(304))->end();
        } else {
          rsp->writeHeader("Content-Type", TYPE_WEBP)
            ->writeHeader("Etag", hash_hex)
            ->end(img);
        }
      });
    });
  }

  template <bool SSL> auto media_routes(
    TemplatedApp<SSL>& app,
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
        const auto id = hex_id_param(req, 0);
        webp_route(req, rsp, std::move(wrap), [&, id](auto cb) { controller->thread_link_card_image(id, std::move(cb)); });
      });
  }

  template auto media_routes<true>(
    TemplatedApp<true>& app,
    shared_ptr<RemoteMediaController> controller
  ) -> void;

  template auto media_routes<false>(
    TemplatedApp<false>& app,
    shared_ptr<RemoteMediaController> controller
  ) -> void;
}
