#pragma once
#include "html_common.h++"
#include "views/webapp/webapp_common.h++"
#include "controllers/search_controller.h++"

namespace Ludwig {

void html_search_result(
  ResponseWriter& r,
  const SearchResultDetail& entry,
  const SiteDetail* site,
  Login login = {},
  bool show_images = true
) noexcept;

void html_search_result_list(
  GenericContext& r,
  std::vector<SearchResultDetail> entries,
  bool show_images = true
);

}