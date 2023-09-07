#pragma once
#include <uWebSockets/App.h>
#include "controller.h++"

namespace Ludwig {
  template <bool SSL> auto webapp_routes(
    uWS::TemplatedApp<SSL>& app,
    Controller& controller
  ) -> void;

  extern template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    Controller& controller
  ) -> void;

  extern template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    Controller& controller
  ) -> void;
}
