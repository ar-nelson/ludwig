#include "site.h++"

using std::optional, std::string, std::string_view;

namespace Ludwig {

static inline auto opt_str(string_view s) -> optional<string> {
  if (s.empty()) return {};
  return string(s);
}

auto SiteDetail::get(ReadTxn& txn) -> SiteDetail {
  const auto name = txn.get_setting_str(SettingsKey::name),
    base_url = txn.get_setting_str(SettingsKey::base_url);
  return {
    .name = name.empty() ? DEFAULT_NAME : string(name),
    .base_url = base_url.starts_with("http") ? string(base_url) : DEFAULT_BASE_URL,
    .description = string(txn.get_setting_str(SettingsKey::description)),
    .public_key_pem = string(txn.get_setting_str(SettingsKey::public_key)),
    .color_accent = opt_str(txn.get_setting_str(SettingsKey::color_accent)).value_or(DEFAULT_COLOR_ACCENT),
    .color_accent_dim = opt_str(txn.get_setting_str(SettingsKey::color_accent_dim)).value_or(DEFAULT_COLOR_ACCENT_DIM),
    .color_accent_hover = opt_str(txn.get_setting_str(SettingsKey::color_accent_hover)).value_or(DEFAULT_COLOR_ACCENT_HOVER),
    .icon_url = opt_str(txn.get_setting_str(SettingsKey::icon_url)),
    .banner_url = opt_str(txn.get_setting_str(SettingsKey::banner_url)),
    .application_question = opt_str(txn.get_setting_str(SettingsKey::application_question)),
    .home_page_type = static_cast<HomePageType>(txn.get_setting_int(SettingsKey::home_page_type)),
    .default_board_id = txn.get_setting_int(SettingsKey::default_board_id),
    .post_max_length = txn.get_setting_int(SettingsKey::post_max_length),
    .remote_post_max_length = txn.get_setting_int(SettingsKey::remote_post_max_length),
    .created_at = txn.get_setting_int(SettingsKey::created_at),
    .updated_at = txn.get_setting_int(SettingsKey::updated_at),
    .setup_done = !!txn.get_setting_int(SettingsKey::setup_done),
    .javascript_enabled = !!txn.get_setting_int(SettingsKey::javascript_enabled),
    .infinite_scroll_enabled = !!txn.get_setting_int(SettingsKey::infinite_scroll_enabled),
    .votes_enabled = !!txn.get_setting_int(SettingsKey::votes_enabled),
    .downvotes_enabled = !!txn.get_setting_int(SettingsKey::downvotes_enabled),
    .cws_enabled = !!txn.get_setting_int(SettingsKey::cws_enabled),
    .require_login_to_view = !!txn.get_setting_int(SettingsKey::require_login_to_view),
    .board_creation_admin_only = !!txn.get_setting_int(SettingsKey::board_creation_admin_only),
    .registration_enabled = !!txn.get_setting_int(SettingsKey::registration_enabled),
    .registration_application_required = !!txn.get_setting_int(SettingsKey::registration_application_required),
    .registration_invite_required = !!txn.get_setting_int(SettingsKey::registration_invite_required),
    .invite_admin_only = !!txn.get_setting_int(SettingsKey::invite_admin_only),
  };
}

}