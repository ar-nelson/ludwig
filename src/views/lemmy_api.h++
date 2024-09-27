#pragma once
#include "controllers/lemmy_api.h++"
#include "util/rate_limiter.h++"
#include <uWebSockets/App.h>

namespace Ludwig::Lemmy {
  template <bool SSL> auto api_routes(
    uWS::TemplatedApp<SSL>& app,
    std::shared_ptr<ApiController> controller,
    std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
  ) -> void;

#ifndef LUDWIG_DEBUG
  extern template auto api_routes<true>(
    uWS::TemplatedApp<true>& app,
    std::shared_ptr<ApiController> controller,
    std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
  ) -> void;
#endif

  extern template auto api_routes<false>(
    uWS::TemplatedApp<false>& app,
    std::shared_ptr<ApiController> controller,
    std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
  ) -> void;
}
