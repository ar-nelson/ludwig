#pragma once
#include "html_common.h++"
#include "views/webapp/webapp_common.h++"
#include "db/page_cursor.h++"
#include "models/board.h++"

namespace Ludwig {

void html_board_list_entry(ResponseWriter& r, const BoardDetail& entry) noexcept;

void html_board_list(
  GenericContext& r,
  PageCursor& cursor,
  std::generator<const BoardDetail&> entries,
  std::string_view base_url,
  BoardSortType sort,
  bool local_only = false
);

}