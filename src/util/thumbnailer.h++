#pragma once
#include "util/common.h++"

namespace Ludwig {

  auto generate_thumbnail(
    std::optional<std::string_view> mimetype,
    std::string_view data,
    uint16_t width,
    uint16_t height = 0
  ) -> std::string;
}
