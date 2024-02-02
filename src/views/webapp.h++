#pragma once
#include "controllers/instance.h++"
#include "util/rate_limiter.h++"
#include <uWebSockets/App.h>

namespace Ludwig {
  template <bool SSL> auto webapp_routes(
    uWS::TemplatedApp<SSL>& app,
    std::shared_ptr<InstanceController> controller,
    std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
  ) -> void;

  extern template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    std::shared_ptr<InstanceController> controller,
    std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
  ) -> void;

  extern template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    std::shared_ptr<InstanceController> controller,
    std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
  ) -> void;
}
