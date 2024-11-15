#pragma once
#include "controllers/remote_media_controller.h++"
#include <uWebSockets/App.h>

namespace Ludwig {

template <bool SSL>
void define_media_routes(
  uWS::TemplatedApp<SSL>& app,
  std::shared_ptr<RemoteMediaController> controller
);

#ifndef LUDWIG_DEBUG
extern template void define_media_routes<true>(
  uWS::TemplatedApp<true>& app,
  std::shared_ptr<RemoteMediaController> controller
);
#endif

extern template void define_media_routes<false>(
  uWS::TemplatedApp<false>& app,
  std::shared_ptr<RemoteMediaController> controller
);

}
