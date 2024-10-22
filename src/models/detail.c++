#include "detail.h++"
#include "db.h++"
#include "util/web.h++"
#include "util/rich_text.h++"
#include <flatbuffers/flatbuffers.h>
#include <static_block.hpp>

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset, std::monostate,
    std::nullopt, std::optional, std::pair, std::reference_wrapper, std::string,
    std::string_view, std::tuple, std::vector, std::visit,
    fmt::operator""_cf; // NOLINT
namespace chrono = std::chrono;
using namespace std::literals;

namespace Ludwig {

  static constexpr uint8_t FETCH_MAX_TRIES = 6;
  static constexpr chrono::seconds FETCH_BACKOFF_DELAYS[FETCH_MAX_TRIES] = { 0s, 1min, 5min, 1h, 24h, 24h * 7 };

  static struct PlaceholderFlatbuffers {
    FlatBufferBuilder fbb;
    Offset<LinkCard> null_link_card;
    Offset<Board> null_board;
    Offset<Thread> null_thread;
    Offset<User> null_user, temp_admin_user;
    Offset<LocalUser> temp_admin_local_user;
    Offset<UserStats> temp_admin_stats;
    Offset<LocalUserStats> temp_admin_local_stats;

    PlaceholderFlatbuffers() {
      fbb.ForceDefaults(true);
      const auto blank = fbb.CreateString("");
      const auto display_name_type = fbb.CreateVector(vector{RichText::Text});
      const auto display_name = fbb.CreateVector(vector{fbb.CreateString("[deleted]").Union()});
      const auto admin = fbb.CreateString("admin");
      {
        LinkCardBuilder card(fbb);
        null_link_card = card.Finish();
      }
      {
        BoardBuilder board(fbb);
        board.add_name(blank);
        board.add_display_name_type(display_name_type);
        board.add_display_name(display_name);
        board.add_can_upvote(false);
        board.add_can_downvote(false);
        null_board = board.Finish();
      }
      {
        UserBuilder user(fbb);
        user.add_name(blank);
        user.add_display_name_type(display_name_type);
        user.add_display_name(display_name);
        null_user = user.Finish();
      }
      {
        UserBuilder user(fbb);
        user.add_name(admin);
        temp_admin_user = user.Finish();
      }
      {
        LocalUserBuilder user(fbb);
        user.add_admin(true);
        temp_admin_local_user = user.Finish();
      }
      {
        UserStatsBuilder stats(fbb);
        temp_admin_stats = stats.Finish();
      }
      {
        LocalUserStatsBuilder stats(fbb);
        temp_admin_local_stats = stats.Finish();
      }
    }
  } placeholders;


  const auto ThreadDetail::null_link_card = get_temporary_pointer(placeholders.fbb, placeholders.null_link_card);
  const auto ThreadDetail::null_board = get_temporary_pointer(placeholders.fbb, placeholders.null_board);
  const auto CommentDetail::null_board = get_temporary_pointer(placeholders.fbb, placeholders.null_board);
  const auto ThreadDetail::null_user = get_temporary_pointer(placeholders.fbb, placeholders.null_user);
  const auto CommentDetail::null_user = get_temporary_pointer(placeholders.fbb, placeholders.null_user);
  const auto CommentDetail::null_thread = get_temporary_pointer(placeholders.fbb, placeholders.null_thread);
  const auto LocalUserDetail::temp_admin_user = get_temporary_pointer(placeholders.fbb, placeholders.temp_admin_user);
  const auto LocalUserDetail::temp_admin_local_user = get_temporary_pointer(placeholders.fbb, placeholders.temp_admin_local_user);
  const auto LocalUserDetail::temp_admin_stats = get_temporary_pointer(placeholders.fbb, placeholders.temp_admin_stats);
  const auto LocalUserDetail::temp_admin_local_stats = get_temporary_pointer(placeholders.fbb, placeholders.temp_admin_local_stats);

  auto ThreadDetail::can_view(Login login) const noexcept -> bool {
    if (mod_state().state >= ModState::Unapproved) {
      if (!login || (login->id != thread().author() && !login->local_user().admin())) return false;
    }
    return true;
  }
  auto CommentDetail::can_view(Login login) const noexcept -> bool {
    if (mod_state().state >= ModState::Unapproved) {
      if (!login || (login->id != comment().author() && !login->local_user().admin())) return false;
    }
    // TODO: Check parent comments
    return true;
  }
  auto UserDetail::can_view(Login login) const noexcept -> bool {
    if (login && login->id == id) return true;
    if (mod_state().state >= ModState::Unapproved) {
      if (!login || !login->local_user().admin()) return false;
    }
    return true;
  }
  auto BoardDetail::can_view(Login login) const noexcept -> bool {
    if (mod_state().state >= ModState::Unapproved) {
      if (!login || !login->local_user().admin()) return false;
    }
    // TODO: Handle private boards
    return true;
  }
  auto ThreadDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || user_hidden || board_hidden || !can_view(login)) return false;
    if (login) {
      if (thread().content_warning() || board().content_warning()) {
        if (login->local_user().hide_cw_posts()) return false;
      }
      if (author().bot() && !login->local_user().show_bot_accounts()) {
        return false;
      }
      // TODO: Hide read posts
    }
    return true;
  }
  auto CommentDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || user_hidden || thread_hidden || board_hidden || !can_view(login)) return false;
    if (login) {
      if (login->local_user().hide_cw_posts()) {
        if (comment().content_warning() || thread().content_warning() || board().content_warning()) {
          return false;
        }
      }
      if (author().bot() && !login->local_user().show_bot_accounts()) {
        return false;
      }
      // TODO: Hide read posts
    }
    // TODO: Check parent comments
    return true;
  }
  auto UserDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || (login && user().bot() && !login->local_user().show_bot_accounts()) || !can_view(login)) return false;
    return true;
  }
  auto BoardDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || !can_view(login)) return false;
    if (login) {
      if (board().content_warning() && login->local_user().hide_cw_posts()) return false;
    }
    return true;
  }
  auto BoardDetail::can_create_thread(Login login) const noexcept -> bool {
    if (!login || login->mod_state(id).state >= ModState::Locked) return false;
    return !board().restricted_posting() || login->local_user().admin();
  }
  auto ThreadDetail::can_reply_to(Login login) const noexcept -> bool {
    if (!login || login->mod_state(thread().board()).state >= ModState::Locked) return false;
    if (login->local_user().admin()) return true;
    return thread().mod_state() < ModState::Locked;
  }
  auto CommentDetail::can_reply_to(Login login) const noexcept -> bool {
    if (!login || login->mod_state(thread().board()).state >= ModState::Locked) return false;
    if (login->local_user().admin()) return true;
    return comment().mod_state() < ModState::Locked &&
      thread().mod_state() < ModState::Locked;
  }
  auto ThreadDetail::can_edit(Login login) const noexcept -> bool {
    if (!login || login->mod_state(thread().board()).state >= ModState::Locked || thread().instance()) return false;
    return login->id == thread().author() || login->local_user().admin();
  }
  auto CommentDetail::can_edit(Login login) const noexcept -> bool {
    if (!login || login->mod_state(thread().board()).state >= ModState::Locked || comment().instance()) return false;
    return login->id == comment().author() || login->local_user().admin();
  }
  auto ThreadDetail::can_delete(Login login) const noexcept -> bool {
    if (!login || login->mod_state(thread().board()).state >= ModState::Locked || thread().instance()) return false;
    return login->id == thread().author() || login->local_user().admin();
  }
  auto CommentDetail::can_delete(Login login) const noexcept -> bool {
    if (!login || login->mod_state(thread().board()).state >= ModState::Locked || comment().instance()) return false;
    return login->id == comment().author() || login->local_user().admin();
  }
  auto ThreadDetail::can_upvote(Login login, const SiteDetail* site) const noexcept -> bool {
    return login && can_view(login) &&
           mod_state().state < ModState::Locked &&
           login->mod_state(thread().board()).state < ModState::Locked &&
           board().can_upvote() && (board().instance() || site->votes_enabled);
  }
  auto CommentDetail::can_upvote(Login login, const SiteDetail* site) const noexcept -> bool {
    return login && can_view(login) &&
           mod_state().state < ModState::Locked &&
           login->mod_state(thread().board()).state < ModState::Locked &&
           board().can_upvote() && (board().instance() || site->votes_enabled);
  }
  auto ThreadDetail::can_downvote(Login login, const SiteDetail* site) const noexcept -> bool {
    return login && can_view(login) &&
           mod_state().state < ModState::Locked &&
           login->mod_state(thread().board()).state < ModState::Locked &&
           board().can_downvote() &&
           (board().instance() || site->downvotes_enabled);
  }
  auto CommentDetail::can_downvote(Login login, const SiteDetail* site) const noexcept -> bool {
    return login && can_view(login) &&
           mod_state().state < ModState::Locked &&
           login->mod_state(thread().board()).state < ModState::Locked &&
           board().can_downvote() &&
           (board().instance() || site->downvotes_enabled);
  }
  auto BoardDetail::should_show_votes(Login, const SiteDetail* site) const noexcept -> bool {
    return site->votes_enabled && board().can_upvote();
  }
  auto ThreadDetail::should_show_votes(Login, const SiteDetail* site) const noexcept -> bool {
    return site->votes_enabled && board().can_upvote();
  }
  auto CommentDetail::should_show_votes(Login, const SiteDetail* site) const noexcept -> bool {
    return site->votes_enabled && board().can_upvote();
  }
  auto UserDetail::can_change_settings(Login login) const noexcept -> bool {
    return maybe_local_user() && login && (login->local_user().admin() || login->id == id);
  }
  auto BoardDetail::can_change_settings(Login login) const noexcept -> bool {
    return maybe_local_board() && login && (login->local_user().admin() || login->id == maybe_local_board()->get().owner());
  }
  auto ThreadDetail::should_fetch_card() const noexcept -> bool {
    if (!thread().content_url()) return false;
    const auto url = Url::parse(thread().content_url()->str());
    if (!url || !url->is_http_s()) return false;
    const auto& card = link_card();
    return !card.fetch_complete() &&
      card.fetch_tries() < FETCH_MAX_TRIES &&
      now_t() > uint_to_timestamp(card.last_fetch_at().value_or(0)) + FETCH_BACKOFF_DELAYS[card.fetch_tries()];
  }

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

  auto UserDetail::get(ReadTxn& txn, uint64_t id, Login login) -> UserDetail {
    const auto user = txn.get_user(id);
    const auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ApiError("User does not exist", 410);
    const auto local_user = txn.get_local_user(id);
    const auto hidden = login && txn.has_user_hidden_user(login->id, id);
    return { id, *user, local_user, *user_stats, hidden };
  }

  auto LocalUserDetail::get(ReadTxn& txn, uint64_t id, Login login) -> LocalUserDetail {
    const auto detail = UserDetail::get(txn, id, login);
    const auto stats = txn.get_local_user_stats(id);
    if (!detail.maybe_local_user() || !stats) throw ApiError("Local user does not exist", 410);
    return { std::move(detail), *stats };
  }

  auto LocalUserDetail::get_login(ReadTxn& txn, uint64_t id) -> LocalUserDetail {
    try { return get(txn, id, {}); }
    catch (const ApiError& e) {
      if (e.http_status == 410) throw ApiError("Logged in user does not exist", 401);
      else throw e;
    }
  }

  auto BoardDetail::get(ReadTxn& txn, uint64_t id, Login login) -> BoardDetail {
    const auto board = txn.get_board(id);
    const auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ApiError("Board does not exist", 410);
    const auto local_board = txn.get_local_board(id);
    const auto hidden = login && txn.has_user_hidden_board(login->id, id);
    const auto subscribed = login && txn.is_user_subscribed_to_board(login->id, id);
    return { id, *board, local_board, *board_stats, hidden, subscribed };
  }

  auto LocalBoardDetail::get(ReadTxn& txn, uint64_t id, Login login) -> LocalBoardDetail {
    const auto detail = BoardDetail::get(txn, id, login);
    if (!detail.maybe_local_board()) throw ApiError("Local user does not exist", 410);
    return { std::move(detail) };
  }

  auto ThreadDetail::get(
    ReadTxn& txn,
    uint64_t thread_id,
    Login login,
    OptRef<User> author,
    bool is_author_hidden,
    OptRef<Board> board,
    bool is_board_hidden
  ) -> ThreadDetail {
    const auto thread = txn.get_thread(thread_id);
    const auto stats = txn.get_post_stats(thread_id);
    if (!thread || !stats) throw ApiError("Database error", 500,
      fmt::format("Entry references nonexistent thread {:x} (database is inconsistent!)", thread_id)
    );
    if (!author) {
      const auto id = thread->get().author();
      author = txn.get_user(id);
      is_author_hidden = login && (
        txn.has_user_hidden_user(login->id, id) ||
        (!login->local_user().show_bot_accounts() && author && author->get().bot())
      );
    }
    if (!board) {
      const auto id = thread->get().board();
      board = txn.get_board(id);
      const auto local_board = txn.get_local_board(id);
      is_board_hidden = (login && txn.has_user_hidden_board(login->id, id)) ||
        (local_board && local_board->get().private_() && (!login || !txn.is_user_subscribed_to_board(login->id, id)));
    }
    const auto card = thread->get().content_url() ? txn.get_link_card(thread->get().content_url()->string_view()) : nullopt;
    const Vote vote = login ? txn.get_vote_of_user_for_post(login->id, thread_id) : Vote::NoVote;
    return {
      .id = thread_id,
      .your_vote = vote,
      .saved = login && txn.has_user_saved_post(login->id, thread_id),
      .hidden = login && txn.has_user_hidden_post(login->id, thread_id),
      .user_hidden = is_author_hidden,
      .board_hidden = is_board_hidden,
      .board_subscribed = login && txn.is_user_subscribed_to_board(login->id, thread->get().board()),
      .user_is_admin = txn.get_local_user(thread->get().author()).transform([](auto u){return u.get().admin();}).value_or(false),
      ._thread = *thread,
      ._stats = *stats,
      ._link_card = card,
      ._author = author,
      ._board = board
    };
  }

  auto CommentDetail::get(
    ReadTxn& txn,
    uint64_t comment_id,
    Login login,
    OptRef<User> author,
    bool is_author_hidden,
    OptRef<Thread> thread,
    bool is_thread_hidden,
    OptRef<Board> board,
    bool is_board_hidden
  ) -> CommentDetail {
    const auto comment = txn.get_comment(comment_id);
    const auto stats = txn.get_post_stats(comment_id);
    if (!comment || !stats) throw ApiError("Database error", 500,
      fmt::format("Entry references nonexistent comment {:x} (database is inconsistent!)", comment_id)
    );
    if (!author) {
      const auto id = comment->get().author();
      author = txn.get_user(id);
      is_author_hidden = login && (
        txn.has_user_hidden_user(login->id, id) ||
        (!login->local_user().show_bot_accounts() && author && author->get().bot())
      );
    }
    if (!thread) {
      const auto id = comment->get().thread();
      thread = txn.get_thread(id);
      is_thread_hidden = login && txn.has_user_hidden_post(login->id, id);
    }
    if (!board) {
      const auto id = thread->get().board();
      board = txn.get_board(id);
      const auto local_board = txn.get_local_board(id);
      is_board_hidden = (login && txn.has_user_hidden_board(login->id, id)) ||
        (local_board && local_board->get().private_() && (!login || !txn.is_user_subscribed_to_board(login->id, id)));
    }
    const Vote vote = login ? txn.get_vote_of_user_for_post(login->id, comment_id) : Vote::NoVote;
    vector<uint64_t> path;
    for (
      auto c = comment;
      c && c->get().parent() != c->get().thread();
      c = txn.get_comment(c->get().parent())
    ) {
      path.insert(path.begin(), c->get().parent());
    }
    return {
      .id = comment_id,
      .your_vote = vote,
      .saved = login && txn.has_user_saved_post(login->id, comment_id),
      .hidden = login && txn.has_user_hidden_post(login->id, comment_id),
      .thread_hidden = is_thread_hidden,
      .user_hidden = is_author_hidden,
      .board_hidden = is_board_hidden,
      .board_subscribed = login && txn.is_user_subscribed_to_board(login->id, thread->get().board()),
      .user_is_admin = txn.get_local_user(comment->get().author()).transform([](auto u){return u.get().admin();}).value_or(false),
      ._comment = *comment,
      ._stats = *stats,
      ._author = author,
      ._thread = thread,
      ._board = board,
      .path = path
    };
  }

  auto NotificationDetail::get(
    ReadTxn& txn,
    uint64_t notification_id,
    const LocalUserDetail& login
  ) -> NotificationDetail {
    const auto notification = txn.get_notification(notification_id);
    if (!notification || notification->get().user() != login.id) {
      throw ApiError("Notification does not exist", 410);
    }
    NotificationDetail detail { .id = notification_id, .notification = *notification };
    auto subject = notification->get().subject();
    switch (notification->get().type()) {
      case NotificationType::MentionInThread:
      case NotificationType::BoostThread:
        detail.subject = ThreadDetail::get(txn, subject.value(), login);
        break;
      case NotificationType::MentionInComment:
      case NotificationType::ReplyToThread:
      case NotificationType::ReplyToComment:
      case NotificationType::BoostComment:
        detail.subject = CommentDetail::get(txn, subject.value(), login);
        break;
      case NotificationType::ApproveSubscription:
      case NotificationType::BecomeMod:
      case NotificationType::SubscribedBoardRemoved:
      case NotificationType::SubscribedBoardDefederated:
        if (subject) {
          if (auto board = txn.get_board(*subject)) {
            detail.subject = *board;
          }
        }
        break;
      case NotificationType::Follow:
      case NotificationType::FollowedUserRemoved:
      case NotificationType::FollowedUserDefererated:
        if (auto user = txn.get_user(subject.value())) {
          detail.subject = *user;
        }
        break;
    }
    return detail;
  }

  auto ThreadDetail::mod_state(PostContext context) const noexcept -> ModStateDetail {
    using enum ModState;
    using enum ModStateSubject;
    // TODO: Board-specific user mod state
    ModStateDetail d;
    if (context != PostContext::Board && board().mod_state() > Normal && board().mod_state() >= d.state) {
      d = { Board, board().mod_state(), opt_sv(board().mod_reason()) };
    }
    if (context != PostContext::User && author().mod_state() > Normal && author().mod_state() >= d.state) {
      d = { User, author().mod_state(), opt_sv(author().mod_reason()) };
    }
    if (thread().board_mod_state() > Normal && thread().board_mod_state() >= d.state) {
      d = { ThreadInBoard, thread().board_mod_state(), opt_sv(thread().board_mod_reason()) };
    }
    if (thread().mod_state() > Normal && thread().mod_state() >= d.state) {
      d = { Thread, thread().mod_state(), opt_sv(thread().mod_reason()) };
    }
    return d;
  }

  auto CommentDetail::mod_state(PostContext context) const noexcept -> ModStateDetail {
    using enum ModState;
    using enum ModStateSubject;
    // TODO: Board-specific user mod state
    ModStateDetail d;
    if (context != PostContext::Board && context != PostContext::Reply && board().mod_state() > Normal && board().mod_state() >= d.state) {
      d = { Board, board().mod_state(), opt_sv(board().mod_reason()) };
    }
    if (context != PostContext::User && author().mod_state() > Normal && author().mod_state() >= d.state) {
      d = { User, author().mod_state(), opt_sv(author().mod_reason()) };
    }
    if (context != PostContext::Reply && thread().board_mod_state() > Normal && thread().board_mod_state() >= d.state) {
      d = { ThreadInBoard, thread().board_mod_state(), opt_sv(thread().board_mod_reason()) };
    }
    if (context != PostContext::Reply && thread().mod_state() > Normal && thread().mod_state() >= d.state) {
      d = { Thread, thread().mod_state(), opt_sv(thread().mod_reason()) };
    }
    if (comment().board_mod_state() > Normal && comment().board_mod_state() >= d.state) {
      d = { CommentInBoard, comment().board_mod_state(), opt_sv(comment().board_mod_reason()) };
    }
    if (comment().mod_state() > Normal && comment().mod_state() >= d.state) {
      d = { Comment, comment().mod_state(), opt_sv(comment().mod_reason()) };
    }
    return d;
  }

  auto ThreadDetail::content_warning(PostContext context) const noexcept -> optional<ContentWarningDetail> {
    using enum ContentWarningSubject;
    if (thread().content_warning()) {
      return {{ Thread, thread().content_warning()->string_view() }};
    } else if (context != PostContext::Board && context != PostContext::View && board().content_warning()) {
      return {{ Board, board().content_warning()->string_view() }};
    }
    return {};
  }

  auto CommentDetail::content_warning(PostContext context) const noexcept -> optional<ContentWarningDetail> {
    using enum ContentWarningSubject;
    if (comment().content_warning()) {
      return {{ Comment, comment().content_warning()->string_view() }};
    } else if (context != PostContext::Reply && thread().content_warning()) {
      return {{ Thread, thread().content_warning()->string_view() }};
    } else if (context != PostContext::Board && context != PostContext::View && context != PostContext::Reply && board().content_warning()) {
      return {{ Board, board().content_warning()->string_view() }};
    }
    return {};
  }

  auto search_result_detail(ReadTxn& txn, const SearchResult& result, Login login) -> optional<SearchResultDetail> {
    using enum SearchResultType;
    const auto id = result.id;
    switch (result.type) {
      case User: {
        const auto entry = UserDetail::get(txn, id, login);
        if (entry.should_show(login)) return entry;
        break;
      }
      case Board: {
        const auto entry = BoardDetail::get(txn, id, login);
        if (entry.should_show(login)) return entry;
        break;
      }
      case Thread: {
        const auto entry = ThreadDetail::get(txn, id, login);
        if (entry.should_show(login)) return entry;
        break;
      }
      case Comment: {
        const auto entry = CommentDetail::get(txn, id, login);
        if (entry.should_show(login)) return entry;
        break;
      }
    }
    return {};
  }
}
