#pragma once
#include "html_common.h++"
#include "models/thread.h++"
#include "models/site.h++"

namespace Ludwig {

void html_thread_entry(
  ResponseWriter& r,
  const ThreadDetail& thread,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept;

}