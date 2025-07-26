#pragma once
#include "controllers/user_controller.h++"
#include "html_common.h++"
#include "views/router_common.h++"
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

auto user_settings_form_to_update(QueryString<std::string_view> form) -> LocalUserUpdate;

void html_user_settings_profile_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  std::optional<std::string_view> error = {}
) noexcept;

auto user_settings_profile_form_to_update(QueryString<std::string_view> form) -> LocalUserUpdate;

void html_user_settings_account_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  std::optional<std::string_view> error = {}
) noexcept;

auto user_settings_validate_change_password_form(QueryString<std::string_view> form) -> std::pair<SecretString, SecretString>;

auto user_settings_validate_delete_account_form(QueryString<std::string_view> form) -> std::pair<SecretString, bool>;

void html_invites_list(
  ResponseWriter& r,
  SessionController& sessions,
  ReadTxn& txn,
  const LocalUserDetail& login,
  std::string_view cursor_str = "",
  std::optional<std::string_view> error = {}
) noexcept;

}