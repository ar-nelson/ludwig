#pragma once
#include "controllers/lemmy_api_controller.h++"
#include "util/rate_limiter.h++"
#include "views/router_common.h++"

namespace Ludwig::Lemmy {

template <bool SSL>
void define_api_routes(
  uWS::TemplatedApp<SSL>& app,
  std::shared_ptr<DB> db,
  std::shared_ptr<ApiController> controller,
  std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
);

#ifndef LUDWIG_DEBUG
extern template void define_api_routes<true>(
  uWS::TemplatedApp<true>& app,
  std::shared_ptr<DB> db,
  std::shared_ptr<ApiController> controller,
  std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
);
#endif

extern template void define_api_routes<false>(
  uWS::TemplatedApp<false>& app,
  std::shared_ptr<DB> db,
  std::shared_ptr<ApiController> controller,
  std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
);

}
