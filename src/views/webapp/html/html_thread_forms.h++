#pragma once
#include "html_common.h++"
#include "models/board.h++"
#include "models/thread.h++"
#include "models/local_user.h++"

namespace Ludwig {

void html_create_thread_form(
  ResponseWriter& r,
  bool show_url,
  const BoardDetail board,
  const LocalUserDetail& login,
  std::optional<std::string_view> error = {}
) noexcept;

void html_edit_thread_form(
  ResponseWriter& r,
  const ThreadDetail& thread,
  const LocalUserDetail& login,
  std::optional<std::string_view> error = {}
) noexcept;

}