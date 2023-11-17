#include "media.h++"
#include "util/web.h++"

using std::shared_ptr, std::string;

namespace Ludwig {
  template <bool SSL> static auto webp_route(
    uWS::HttpRequest* req,
    uWS::HttpResponse<SSL>* rsp,
    asio::awaitable<ThumbnailCache::Image>&& await_img
  ) -> asio::awaitable<void> {
    const string if_none_match(req->getHeader("if-none-match"));
    const auto img = co_await std::move(await_img);
    const auto& [data, hash] = *img;
    const auto hash_hex = fmt::format("\"{:016x}\"", hash);
    if (if_none_match == hash_hex) {
      rsp->writeStatus(http_status(304))->end();
    } else {
      rsp->writeHeader("Content-Type", TYPE_WEBP)
        ->writeHeader("Etag", hash_hex)
        ->end(data);
    }
  }

  template <bool SSL> auto media_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<RemoteMediaController> controller
  ) -> void {
    Router(app)
      .get_async("/media/user/:name/avatar.webp", [controller](auto* rsp, auto* req, auto) -> asio::awaitable<void> {
        co_await webp_route(req, rsp, controller->user_avatar(req->getParameter(0)));
      })
      .get_async("/media/user/:name/banner.webp", [controller](auto* rsp, auto* req, auto) -> asio::awaitable<void> {
        co_await webp_route(req, rsp, controller->user_banner(req->getParameter(0)));
      })
      .get_async("/media/board/:name/icon.webp", [controller](auto* rsp, auto* req, auto) -> asio::awaitable<void> {
        co_await webp_route(req, rsp, controller->board_icon(req->getParameter(0)));
      })
      .get_async("/media/board/:name/banner.webp", [controller](auto* rsp, auto* req, auto) -> asio::awaitable<void> {
        co_await webp_route(req, rsp, controller->board_banner(req->getParameter(0)));
      })
      .get_async("/media/thread/:id/thumbnail.webp", [controller](auto* rsp, auto* req, auto) -> asio::awaitable<void> {
        const auto id = hex_id_param(req, 0);
        co_await webp_route(req, rsp, controller->thread_link_card_image(id));
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
