#pragma once
#include "html_common.h++"
#include "controllers/first_run_controller.h++"

namespace Ludwig {

void html_first_run_setup_form(
  ResponseWriter& r,
  const FirstRunSetupOptions& options,
  std::optional<std::string_view> error = {}
) noexcept;

}