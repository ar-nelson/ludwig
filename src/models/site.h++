#pragma once
#include "db/db.h++"
#include "models/enums_pre.h++"

namespace Ludwig {

struct SiteDetail {
  static inline const std::string
    DEFAULT_COLOR_ACCENT = "#1077c1", // hsl(205, 85%, 41%)
    DEFAULT_COLOR_ACCENT_DIM = "#73828c", // hsl(205, 10%, 50%)
    DEFAULT_COLOR_ACCENT_HOVER = "#085e9b", // hsl(205, 90%, 32%)
    DEFAULT_NAME = "Ludwig",
    DEFAULT_BASE_URL = "http://localhost:2023";

  std::string name, base_url, description, public_key_pem, color_accent, color_accent_dim, color_accent_hover;
  std::optional<std::string> icon_url, banner_url, application_question;
  HomePageType home_page_type;
  uint64_t default_board_id, post_max_length, remote_post_max_length, created_at, updated_at;
  bool setup_done, javascript_enabled, infinite_scroll_enabled, votes_enabled,
      downvotes_enabled, cws_enabled, require_login_to_view,
      board_creation_admin_only, registration_enabled,
      registration_application_required, registration_invite_required,
      invite_admin_only;

  static auto get(ReadTxn& txn) -> SiteDetail;
};

}