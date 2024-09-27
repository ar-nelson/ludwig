#pragma once
#include "controllers/remote_media.h++"
#include <uWebSockets/App.h>

namespace Ludwig {
  template <bool SSL> auto media_routes(
    uWS::TemplatedApp<SSL>& app,
    std::shared_ptr<RemoteMediaController> controller
  ) -> void;

#ifndef LUDWIG_DEBUG
  extern template auto media_routes<true>(
    uWS::TemplatedApp<true>& app,
    std::shared_ptr<RemoteMediaController> controller
  ) -> void;
#endif

  extern template auto media_routes<false>(
    uWS::TemplatedApp<false>& app,
    std::shared_ptr<RemoteMediaController> controller
  ) -> void;
}
