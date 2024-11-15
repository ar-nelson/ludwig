#pragma once
#include "html_common.h++"
#include "models/site.h++"

namespace Ludwig {

void html_login_form(ResponseWriter& r, std::optional<std::string_view> error = {}) noexcept;

void html_sidebar_login_form(ResponseWriter& r) noexcept;

void html_register_form(ResponseWriter& r, const SiteDetail* site, std::optional<std::string_view> error = {}) noexcept;

}