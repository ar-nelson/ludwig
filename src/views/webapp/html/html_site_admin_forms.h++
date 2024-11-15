#pragma once
#include "html_common.h++"
#include "views/router_common.h++"
#include "models/site.h++"
#include "controllers/session_controller.h++"

namespace Ludwig {

enum class SiteAdminTab : uint8_t {
  Settings,
  ImportExport,
  Applications,
  Invites
};

void html_site_admin_tabs(ResponseWriter& r, const SiteDetail* site, SiteAdminTab selected) noexcept;

void html_site_admin_form(
  ResponseWriter& r,
  const SiteDetail* site,
  std::optional<std::string_view> error = {}
) noexcept;

void html_site_admin_import_export_form(ResponseWriter& r) noexcept;

void html_site_admin_applications_list(
  ResponseWriter& r,
  SessionController& sessions,
  ReadTxn& txn,
  Login login,
  std::optional<uint64_t> cursor = {},
  std::optional<std::string_view> error = {}
) noexcept;

auto form_to_site_update(QueryString<std::string_view> body) -> SiteUpdate;

}