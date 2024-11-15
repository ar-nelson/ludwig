#pragma once
#include "html_common.h++"
#include "models/site.h++"
#include "models/local_user.h++"
#include "controllers/session_controller.h++"

namespace Ludwig {

enum class UserSettingsTab : uint8_t {
  Settings,
  Profile,
  Account,
  Invites
};

void html_user_settings_tabs(ResponseWriter& r, const SiteDetail* site, UserSettingsTab selected) noexcept;

void html_user_settings_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  std::optional<std::string_view> error = {}
) noexcept;

void html_user_settings_profile_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  std::optional<std::string_view> error = {}
) noexcept;

void html_user_settings_account_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  std::optional<std::string_view> error = {}
) noexcept;

void html_invites_list(
  ResponseWriter& r,
  SessionController& sessions,
  ReadTxn& txn,
  const LocalUserDetail& login,
  std::string_view cursor_str = "",
  std::optional<std::string_view> error = {}
) noexcept;

}