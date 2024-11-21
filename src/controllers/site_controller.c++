#include "site_controller.h++"
#include "models/local_user.h++"
#include <atomic>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

using std::optional, std::shared_ptr, std::string, std::string_view,
  flatbuffers::FlatBufferBuilder;

namespace Ludwig {

static const std::regex color_hex_regex(R"(#[0-9a-f]{6})", std::regex::icase);

SiteController::SiteController(
  shared_ptr<DB> db,
  shared_ptr<EventBus> event_bus
) : db(db), event_bus(event_bus) {
  auto txn = db->open_read_txn();
  auto detail = new SiteDetail;
  *detail = SiteDetail::get(txn);
  cached_site_detail.store(detail, std::memory_order_release);
}

SiteController::~SiteController() {
  const SiteDetail* null = nullptr;
  auto ptr = cached_site_detail.exchange(null, std::memory_order_acq_rel);
  if (ptr) delete ptr;
}

auto SiteController::can_change_site_settings(Login login) -> bool {
  return login && login->local_user().admin();
}

auto SiteController::can_create_board(Login login, const SiteDetail& site) -> bool {
  return login && ((!site.board_creation_admin_only &&
                    login->mod_state().state < ModState::Locked) ||
                    login->local_user().admin());
}

auto SiteController::update_site(WriteTxn txn, const SiteUpdate& update, optional<uint64_t> as_user) -> void {
  using namespace SettingsKey;
  update.validate();
  if (as_user && !can_change_site_settings(LocalUserDetail::get_login(txn, *as_user))) {
    throw ApiError("User does not have permission to change site settings", 403);
  }
  if (const auto v = update.name) txn.set_setting(name, *v);
  if (const auto v = update.description) txn.set_setting(description, *v);
  if (const auto v = update.icon_url) txn.set_setting(icon_url, v->value_or(""));
  if (const auto v = update.banner_url) txn.set_setting(banner_url, v->value_or(""));
  if (const auto v = update.application_question) txn.set_setting(application_question, v->value_or(""));
  if (const auto v = update.post_max_length) txn.set_setting(post_max_length, *v);
  if (const auto v = update.remote_post_max_length) txn.set_setting(remote_post_max_length, *v);
  if (const auto v = update.home_page_type) txn.set_setting(home_page_type, (uint64_t)*v);
  if (const auto v = update.votes_enabled) txn.set_setting(votes_enabled, *v);
  if (const auto v = update.downvotes_enabled) txn.set_setting(downvotes_enabled, *v);
  if (const auto v = update.javascript_enabled) txn.set_setting(javascript_enabled, *v);
  if (const auto v = update.infinite_scroll_enabled) txn.set_setting(infinite_scroll_enabled, *v);
  if (const auto v = update.board_creation_admin_only) txn.set_setting(board_creation_admin_only, *v);
  if (const auto v = update.registration_enabled) txn.set_setting(registration_enabled, *v);
  if (const auto v = update.registration_application_required) txn.set_setting(registration_application_required, *v);
  if (const auto v = update.registration_invite_required) txn.set_setting(registration_invite_required, *v);
  if (const auto v = update.invite_admin_only) txn.set_setting(invite_admin_only, *v);
  if (const auto v = update.color_accent) txn.set_setting(color_accent, *v);
  if (const auto v = update.color_accent_dim) txn.set_setting(color_accent_dim, *v);
  if (const auto v = update.color_accent_hover) txn.set_setting(color_accent_hover, *v);
  txn.set_setting(updated_at, now_s());
  auto detail = new SiteDetail;
  *detail = SiteDetail::get(txn);
  txn.commit();
  auto old_detail = cached_site_detail.exchange(detail, std::memory_order_acq_rel);
  if (old_detail) delete old_detail;
  event_bus->dispatch(Event::SiteUpdate);
}

auto SiteUpdate::validate() const -> void {
  if (icon_url && *icon_url) {
    if (const auto url = ada::parse(string(**icon_url))) {
      if (!is_https(url)) throw ApiError("Icon URL must be HTTP(S)", 400);
    } else throw ApiError("Icon URL is not a valid URL", 400);
  }
  if (banner_url && *banner_url) {
    if (const auto url = ada::parse(string(**banner_url))) {
      if (!is_https(url)) throw ApiError("Banner URL must be HTTP(S)", 400);
    } else throw ApiError("Banner URL is not a valid URL", 400);
  }
  if (post_max_length && *post_max_length < 512) {
    throw ApiError("Max post length cannot be less than 512", 400);
  }
  if (remote_post_max_length && *remote_post_max_length < 512) {
    throw ApiError("Max remote post length cannot be less than 512", 400);
  }
  if (
    (color_accent && !regex_match(color_accent->begin(), color_accent->end(), color_hex_regex)) ||
    (color_accent_dim && !regex_match(color_accent_dim->begin(), color_accent_dim->end(), color_hex_regex)) ||
    (color_accent_hover && !regex_match(color_accent_hover->begin(), color_accent_hover->end(), color_hex_regex))
  ) {
    throw ApiError("Colors must be in hex format", 400);
  }
}

}