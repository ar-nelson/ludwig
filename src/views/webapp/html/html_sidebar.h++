#pragma once
#include "html_common.h++"
#include "models/site.h++"
#include "models/user.h++"
#include "models/board.h++"

namespace Ludwig {

void html_subscribe_button(ResponseWriter& r, std::string_view name, bool is_unsubscribe) noexcept;

void html_sidebar(
  ResponseWriter& r,
  Login login,
  const SiteDetail* site,
  std::variant<std::monostate, const BoardDetail, const UserDetail> detail = std::monostate()
) noexcept;

}