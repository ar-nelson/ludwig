#include "media_routes.h++"
#include "router_common.h++"

using std::pair, std::shared_ptr, std::string;
using namespace uWS;

namespace Ludwig {

template <bool SSL>
static inline auto write_thumbnail(
  HttpResponse<SSL>* rsp,
  const string& if_none_match,
  const ImageRef& img
) -> void {
  if (!img) throw ApiError("No thumbnail available", 404);
  const auto hash_hex = fmt::format(R"("{:016x}")", img.hash());
  if (if_none_match == hash_hex) {
    rsp->writeStatus(http_status(304))->end();
  } else {
    rsp->writeHeader("Content-Type", TYPE_WEBP)
      ->writeHeader("Etag", hash_hex)
      ->end(img);
  }
}

auto name_and_if_none_match(HttpRequest* req) -> pair<string, string> {
  return pair(string(req->getParameter(0)), string(req->getHeader("if-none-match")));
}

template <bool SSL>
void define_media_routes(
  TemplatedApp<SSL>& app,
  shared_ptr<RemoteMediaController> controller
) {
  using Coro = RouterCoroutine<RequestContext<SSL>>;
  Router(app, {})
    .get_async("/media/user/:name/avatar.webp", [controller](auto* rsp, auto ctx) -> Coro {
      auto [name, if_none_match] = co_await ctx.with_request(name_and_if_none_match);
      write_thumbnail(rsp, if_none_match, co_await controller->user_avatar(name));
    })
    .get_async("/media/user/:name/banner.webp", [controller](auto* rsp, auto ctx) -> Coro {
      auto [name, if_none_match] = co_await ctx.with_request(name_and_if_none_match);
      write_thumbnail(rsp, if_none_match, co_await controller->user_banner(name));
    })
    .get_async("/media/board/:name/icon.webp", [controller](auto* rsp, auto ctx) -> Coro {
      auto [name, if_none_match] = co_await ctx.with_request(name_and_if_none_match);
      write_thumbnail(rsp, if_none_match, co_await controller->board_icon(name));
    })
    .get_async("/media/board/:name/banner.webp", [controller](auto* rsp, auto ctx) -> Coro {
      auto [name, if_none_match] = co_await ctx.with_request(name_and_if_none_match);
      write_thumbnail(rsp, if_none_match, co_await controller->board_banner(name));
    })
    .get_async("/media/thread/:id/thumbnail.webp", [controller](auto* rsp, auto ctx) -> Coro {
      auto [id, if_none_match] = co_await ctx.with_request([](auto* req){
        return pair(hex_id_param(req, 0), string(req->getHeader("if-none-match")));
      });
      write_thumbnail(rsp, if_none_match, co_await controller->thread_link_card_image(id));
    });

}

#ifndef LUDWIG_DEBUG
template void define_media_routes<true>(
  TemplatedApp<true>& app,
  shared_ptr<RemoteMediaController> controller
);
#endif

template void define_media_routes<false>(
  TemplatedApp<false>& app,
  shared_ptr<RemoteMediaController> controller
);

}
