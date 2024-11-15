#pragma once
#include "html_common.h++"
#include "views/webapp/webapp_common.h++"
#include "db/page_cursor.h++"
#include "models/user.h++"

namespace Ludwig {

void html_user_list_entry(ResponseWriter& r, const UserDetail& entry, Login login = {}) noexcept;

void html_user_list(
  GenericContext& r,
  PageCursor& cursor,
  std::generator<const UserDetail&> entries,
  std::string_view base_url,
  UserSortType sort,
  bool local_only = false
);

}