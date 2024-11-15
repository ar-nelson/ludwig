#pragma once
#include "models/site.h++"
#include "models/user.h++"
#include "services/event_bus.h++"
#include <atomic>

namespace Ludwig {

struct SiteUpdate {
  std::optional<std::string_view> name, description, color_accent,
      color_accent_dim, color_accent_hover;
  std::optional<std::optional<std::string_view>> icon_url, banner_url,
      application_question;
  std::optional<uint64_t> post_max_length, remote_post_max_length;
  std::optional<HomePageType> home_page_type;
  std::optional<bool> javascript_enabled, infinite_scroll_enabled,
      votes_enabled, downvotes_enabled, cws_enabled, require_login_to_view,
      board_creation_admin_only, registration_enabled,
      registration_application_required, registration_invite_required,
      invite_admin_only;

  auto validate() const -> void;
};

class SiteController {
private:
  std::shared_ptr<DB> db;
  std::shared_ptr<EventBus> event_bus;
  std::atomic<const SiteDetail*> cached_site_detail;

public:
  SiteController(std::shared_ptr<DB> db, std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>());
  ~SiteController();

  static auto can_change_site_settings(Login login) -> bool;
  static auto can_create_board(Login login, const SiteDetail& site) -> bool;
  auto site_detail() const noexcept -> const SiteDetail* {
    return cached_site_detail.load(std::memory_order_acquire);
  }

  // update_site consumes its WriteTxn because, upon txn.commit(), it updates cached_site_detail.
  // If update_site did not call txn.commit(), cached_site_detail would be out of sync with
  // the actual site settings from the perspective of txn while txn remains live.
  auto update_site(WriteTxn txn, const SiteUpdate& update, std::optional<uint64_t> as_user) -> void;
};

}