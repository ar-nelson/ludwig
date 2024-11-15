#pragma once
#include "html_common.h++"
#include "models/site.h++"
#include "models/local_board.h++"

namespace Ludwig {

void html_create_board_form(
  ResponseWriter& r,
  const SiteDetail* site,
  std::optional<std::string_view> error = {}
) noexcept;

void html_board_settings_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalBoardDetail& board,
  std::optional<std::string_view> error = {}
) noexcept;

}