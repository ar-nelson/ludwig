#include "instance.h++"
#include "util/web.h++"
#include <mutex>
#include <regex>
#include <duthomhas/csprng.hpp>
#include <openssl/evp.h>

using std::function, std::nullopt, std::regex, std::regex_match, std::optional,
      std::shared_ptr, std::string, std::string_view, flatbuffers::FlatBufferBuilder;

namespace Ludwig {
  // PBKDF2-HMAC-SHA256 iteration count, as suggested by
  // https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html#pbkdf2
  static constexpr uint32_t PASSWORD_HASH_ITERATIONS = 600'000;

  //static constexpr uint64_t JWT_DURATION = 86400; // 1 day
  static constexpr double RANK_GRAVITY = 1.8;

  static const regex username_regex(R"([a-z0-9_]{1,64})", regex::ECMAScript);
  static const regex email_regex(
    R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|")"
    R"((?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@)"
    R"((?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|)"
    R"(\[(?:(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9]))\.){3})"
    R"((?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])|[a-z0-9-]*[a-z0-9]:)"
    R"((?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))",
    regex::ECMAScript | regex::icase
  );

  static inline auto make_null_board() -> FlatBufferBuilder {
    FlatBufferBuilder fbb;
    auto name_s = fbb.CreateString(""), display_name_s = fbb.CreateString("[deleted]");
    BoardBuilder board(fbb);
    board.add_name(name_s);
    board.add_display_name(display_name_s);
    board.add_created_at(0);
    board.add_can_upvote(false);
    board.add_can_downvote(false);
    fbb.Finish(board.Finish());
    return fbb;
  }
  static inline auto make_null_user() -> FlatBufferBuilder {
    FlatBufferBuilder fbb;
    auto name_s = fbb.CreateString(""), display_name_s = fbb.CreateString("[deleted]");
    UserBuilder user(fbb);
    user.add_name(name_s);
    user.add_display_name(display_name_s);
    user.add_created_at(0);
    fbb.Finish(user.Finish());
    return fbb;
  }
  static inline auto make_null_thread() -> FlatBufferBuilder {
    FlatBufferBuilder fbb;
    auto content_s = fbb.CreateString("[deleted]");
    ThreadBuilder thread(fbb);
    thread.add_author(0);
    thread.add_board(0);
    thread.add_created_at(0);
    thread.add_content_text_raw(content_s);
    thread.add_content_text_safe(content_s);
    thread.add_title(content_s);
    fbb.Finish(thread.Finish());
    return fbb;
  }

  FlatBufferBuilder ThreadListEntry::null_board = make_null_board();
  FlatBufferBuilder ThreadListEntry::null_user = make_null_user();
  FlatBufferBuilder CommentListEntry::null_board = make_null_board();
  FlatBufferBuilder CommentListEntry::null_user = make_null_user();
  FlatBufferBuilder CommentListEntry::null_thread = make_null_thread();

  static inline auto rank_numerator(int64_t karma) -> double {
    return std::log(std::max<int64_t>(1, 3 + karma));
  }
  static inline auto rank_denominator(uint64_t time_diff) -> double {
    const uint64_t age_in_hours = time_diff / 3600;
    return std::pow(age_in_hours + 2, RANK_GRAVITY);
  }
  static auto thread_rank_cmp(const ThreadListEntry& a, const ThreadListEntry& b) -> bool {
    return a.rank > b.rank || (a.rank == b.rank && a.id > b.id);
  }
  static auto comment_rank_cmp(const CommentListEntry& a, const CommentListEntry& b) -> bool {
    return a.rank > b.rank || (a.rank == b.rank && a.id > b.id);
  }
  static inline auto next_cursor_thread_new(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto thread = txn.get_thread(*from_id);
    if (!thread) return Cursor(prefix, 0, 0);
    return Cursor(prefix, thread->get().created_at(), (*from_id) - 1);
  }
  static inline auto next_cursor_comment_new(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto comment = txn.get_comment(*from_id);
    if (!comment) return Cursor(prefix, 0, 0);
    return Cursor(prefix, comment->get().created_at(), (*from_id) - 1);
  }
  static inline auto next_cursor_top(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto stats = txn.get_post_stats(*from_id);
    if (!stats) return Cursor(prefix, 0, 0);
    return Cursor(prefix, karma_uint(stats->get().karma()), (*from_id) - 1);
  }
  static inline auto next_cursor_new_comments(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto stats = txn.get_post_stats(*from_id);
    if (!stats) return Cursor(prefix, 0, 0);
    return Cursor(prefix, stats->get().latest_comment(), (*from_id) - 1);
  }
  static inline auto next_cursor_most_comments(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto stats = txn.get_post_stats(*from_id);
    if (!stats) return Cursor(prefix, 0, 0);
    return Cursor(prefix, stats->get().descendant_count(), (*from_id) - 1);
  }
  auto InstanceController::get_thread_entry(
    ReadTxnBase& txn,
    uint64_t thread_id,
    InstanceController::Login login,
    OptRef<User> author,
    bool is_author_hidden,
    OptRef<Board> board,
    bool is_board_hidden
  ) -> ThreadListEntry {
    const auto thread = txn.get_thread(thread_id);
    const auto stats = txn.get_post_stats(thread_id);
    if (!thread || !stats) {
      spdlog::error("Entry references nonexistent thread {:x} (database is inconsistent!)", thread_id);
      throw ControllerError("Database error", 500);
    }
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
    const Vote vote = login ? txn.get_vote_of_user_for_post(login->id, thread_id) : Vote::NoVote;
    return {
      .id = thread_id,
      .your_vote = vote,
      .saved = login && txn.has_user_saved_post(login->id, thread_id),
      .hidden = login && txn.has_user_hidden_post(login->id, thread_id),
      .user_hidden = is_author_hidden,
      .board_hidden = is_board_hidden,
      ._thread = *thread,
      ._stats = *stats,
      ._author = author,
      ._board = board
    };
  }
  auto InstanceController::get_comment_entry(
    ReadTxnBase& txn,
    uint64_t comment_id,
    InstanceController::Login login,
    OptRef<User> author,
    bool is_author_hidden,
    OptRef<Thread> thread,
    bool is_thread_hidden,
    OptRef<Board> board,
    bool is_board_hidden
  ) -> CommentListEntry {
    const auto comment = txn.get_comment(comment_id);
    const auto stats = txn.get_post_stats(comment_id);
    if (!comment || !stats) {
      spdlog::error("Entry references nonexistent comment {:x} (database is inconsistent!)", comment_id);
      throw ControllerError("Database error", 500);
    }
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
    return {
      .id = comment_id,
      .your_vote = vote,
      .saved = login && txn.has_user_saved_post(login->id, comment_id),
      .hidden = login && txn.has_user_hidden_post(login->id, comment_id),
      .thread_hidden = is_thread_hidden,
      .user_hidden = is_author_hidden,
      .board_hidden = is_board_hidden,
      ._comment = *comment,
      ._stats = *stats,
      ._author = author,
      ._thread = thread,
      ._board = board
    };
  }

  template <typename T, typename Cmp> struct RankedPage {
    std::set<T, Cmp> page;
    optional<uint64_t> next;
  };
  template <typename T, typename Cmp> static auto ranked_page(
    ReadTxnBase& txn,
    DBIter<uint64_t> iter_by_new,
    DBIter<uint64_t> iter_by_top,
    InstanceController::Login login,
    bool skip_cw,
    function<T (uint64_t)> get_entry,
    function<uint64_t (T&)> get_timestamp,
    Cmp cmp,
    optional<uint64_t> from = {},
    size_t page_size = ITEMS_PER_PAGE
  ) -> RankedPage<T, Cmp> {
    int64_t max_possible_karma;
    if (iter_by_top.is_done() || iter_by_new.is_done()) return {};
    {
      auto top_stats = txn.get_post_stats(*iter_by_top);
      if (!top_stats) return {};
      max_possible_karma = top_stats->get().karma();
    }
    const double max_rank = from ? reinterpret_cast<double&>(*from) : INFINITY;
    const auto max_possible_numerator = rank_numerator(max_possible_karma);
    const auto now = now_s();
    bool skipped_any = false;
    std::set<T, Cmp> sorted_entries(cmp);
    // TODO: Make this more performant by iterating pairs of <id, timestamp>
    for (auto id : iter_by_new) {
      try {
        auto entry = get_entry(id);
        if (!InstanceController::should_show(entry, login, skip_cw)) continue;
        const auto timestamp = get_timestamp(entry);
        const auto denominator = rank_denominator(timestamp < now ? now - timestamp : 0);
        entry.rank = rank_numerator(entry.stats().karma()) / denominator;
        if (entry.rank >= max_rank) continue;
        if (sorted_entries.size() > page_size) {
          skipped_any = true;
          const double max_possible_rank = max_possible_numerator / denominator;
          auto last = std::prev(sorted_entries.end());
          if (max_possible_rank <= last->rank) break;
          sorted_entries.erase(last);
        }
        sorted_entries.insert(entry);
      } catch (ControllerError e) {
        continue;
      }
    }
    return {
      sorted_entries,
      skipped_any
        ? optional(reinterpret_cast<const uint64_t&>(std::prev(sorted_entries.end())->rank))
        : nullopt
    };
  }

  static auto comment_tree(
    ReadTxnBase& txn,
    CommentTree& tree,
    uint64_t parent,
    CommentSortType sort,
    InstanceController::Login login,
    bool skip_cw,
    OptRef<Thread> thread = {},
    bool is_thread_hidden = false,
    OptRef<Board> board = {},
    bool is_board_hidden = false,
    optional<uint64_t> from_id = {},
    size_t max_comments = ITEMS_PER_PAGE * 4,
    size_t max_depth = 5
  ) -> void {
    if (!max_depth) {
      tree.mark_continued(parent);
      return;
    }
    if (tree.size() >= max_comments) return;
    optional<DBIter<uint64_t>> iter;
    switch (sort) {
      case CommentSortType::Hot: {
        auto ranked = ranked_page<CommentListEntry>(
          txn,
          txn.list_comments_of_post_new(parent),
          txn.list_comments_of_post_top(parent),
          login,
          skip_cw,
          [&](uint64_t id) { return InstanceController::get_comment_entry(txn, id, login, {}, false, thread, is_thread_hidden, board, is_board_hidden); },
          [&](auto& e) { return e.comment().created_at(); },
          comment_rank_cmp,
          from_id,
          max_comments - tree.size()
        );
        for (auto entry : ranked.page) {
          if (tree.size() >= max_comments) {
            tree.mark_continued(parent, entry.id);
            return;
          }
          const auto id = entry.id, children = entry.stats().child_count();
          tree.emplace(parent, entry);
          if (children) comment_tree(txn, tree, id, sort, login, skip_cw, thread, is_thread_hidden, board, is_board_hidden, {}, max_comments, max_depth - 1);
        }
        if (ranked.next) tree.mark_continued(parent, *ranked.next);
        return;
      }
      case CommentSortType::New:
        iter = txn.list_comments_of_post_new(parent, next_cursor_comment_new(txn, parent, from_id));
        break;
      case CommentSortType::Old:
        throw ControllerError("Sort type not yet implemented", 500);
      case CommentSortType::Top:
        iter = txn.list_comments_of_post_top(parent, next_cursor_top(txn, parent, from_id));
        break;
    }
    assert(!!iter);
    for (auto id : *iter) {
      if (tree.size() >= max_comments) {
        tree.mark_continued(parent, id);
        return;
      }
      auto entry = InstanceController::get_comment_entry(txn, id, login, {}, false, thread, is_thread_hidden, board, is_board_hidden);
      if (!InstanceController::should_show(entry, login, skip_cw)) continue;
      const auto children = entry.stats().child_count();
      tree.emplace(parent, entry);
      if (children) comment_tree(txn, tree, id, sort, login, skip_cw, thread, is_thread_hidden, board, is_board_hidden, {}, max_comments, max_depth - 1);
    }
    if (!iter->is_done()) tree.mark_continued(parent, iter->get_cursor()->int_field_2());
  }

  static inline auto expect_post_stats(ReadTxnBase& txn, uint64_t post_id) -> const PostStats& {
    const auto stats = txn.get_post_stats(post_id);
    if (!stats) {
      spdlog::error("Post {:x} has no corresponding post_stats (database is inconsistent!)", post_id);
      throw ControllerError("Database error");
    }
    return *stats;
  }

  InstanceController::InstanceController(shared_ptr<DB> db) : db(db) {
    auto txn = db->open_read_txn();
    cached_site_detail.domain = string(txn.get_setting_str(SettingsKey::domain));
    cached_site_detail.name = string(txn.get_setting_str(SettingsKey::name));
    cached_site_detail.description = string(txn.get_setting_str(SettingsKey::description));
  }

  auto InstanceController::hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[32]) -> void {
    if (!PKCS5_PBKDF2_HMAC(
      password.str.data(), password.str.length(),
      salt, 16,
      PASSWORD_HASH_ITERATIONS, EVP_sha256(),
      32, hash
    )) {
      throw std::runtime_error("Allocation failure when hashing password");
    }
  }

  auto InstanceController::should_show(const ThreadListEntry& thread, Login login, bool hide_cw) -> bool {
    if (thread.thread().mod_state() >= ModState::Removed) {
      if (!login || (login->id != thread.thread().author() && !login->local_user().admin())) return false;
    }
    if (thread.thread().content_warning() || thread.board().content_warning()) {
      if (hide_cw || (login && login->local_user().hide_cw_posts())) return false;
    }
    // TODO: Check if hidden
    return true;
  }
  auto InstanceController::should_show(const CommentListEntry& comment, Login login, bool hide_cw) -> bool {
    if (comment.comment().mod_state() >= ModState::Removed) {
      if (!login || (login->id != comment.comment().author() && !login->local_user().admin())) return false;
    }
    if (comment.thread().mod_state() >= ModState::Removed) {
      if (!login || (login->id != comment.thread().author() && !login->local_user().admin())) return false;
    }
    if (comment.comment().content_warning() || comment.thread().content_warning() ||
        comment.board().content_warning()) {
      if (hide_cw || (login && login->local_user().hide_cw_posts())) return false;
    }
    // TODO: Check parent comments
    // TODO: Check if hidden
    return true;
  }
  auto InstanceController::should_show(const BoardListEntry& board, Login login, bool hide_cw) -> bool {
    if (board.board().content_warning()) {
      if (hide_cw || (login && login->local_user().hide_cw_posts())) return false;
    }
    // TODO: Check if hidden
    return true;
  }
  auto InstanceController::can_create_thread(const BoardListEntry& board, Login login) -> bool {
    if (!login) return false;
    return !board.board().restricted_posting() ||login->local_user().admin();
  }
  auto InstanceController::can_reply_to(const ThreadListEntry& thread, Login login) -> bool {
    if (!login) return false;
    if (login->local_user().admin()) return true;
    return thread.thread().mod_state() < ModState::Locked;
  }
  auto InstanceController::can_reply_to(const CommentListEntry& comment, Login login) -> bool {
    if (!login) return false;
    if (login->local_user().admin()) return true;
    return comment.comment().mod_state() < ModState::Locked &&
      comment.thread().mod_state() < ModState::Locked;
  }
  auto InstanceController::can_edit(const ThreadListEntry& thread, Login login) -> bool {
    if (!login || thread.thread().instance()) return false;
    return login->id == thread.thread().author() || login->local_user().admin();
  }
  auto InstanceController::can_edit(const CommentListEntry& comment, Login login) -> bool {
    if (!login || comment.comment().instance()) return false;
    return login->id == comment.comment().author() || login->local_user().admin();
  }
  auto InstanceController::can_delete(const ThreadListEntry& thread, Login login) -> bool {
    if (!login || thread.thread().instance()) return false;
    return login->id == thread.thread().author() || login->local_user().admin();
  }
  auto InstanceController::can_delete(const CommentListEntry& comment, Login login) -> bool {
    if (!login || comment.comment().instance()) return false;
    return login->id == comment.comment().author() || login->local_user().admin();
  }
  auto InstanceController::can_upvote(const ThreadListEntry& thread, Login login) -> bool {
    return login && thread.thread().mod_state() < ModState::Locked &&
      thread.board().can_upvote();
  }
  auto InstanceController::can_upvote(const CommentListEntry& comment, Login login) -> bool {
    return login && comment.comment().mod_state() < ModState::Locked &&
      comment.thread().mod_state() < ModState::Locked && comment.board().can_upvote();
  }
  auto InstanceController::can_downvote(const ThreadListEntry& thread, Login login) -> bool {
    return login && thread.thread().mod_state() < ModState::Locked &&
      thread.board().can_downvote();
  }
  auto InstanceController::can_downvote(const CommentListEntry& comment, Login login) -> bool {
    return login && comment.comment().mod_state() < ModState::Locked &&
      comment.thread().mod_state() < ModState::Locked &&
      comment.board().can_downvote();
  }
  auto InstanceController::can_change_board_settings(const LocalBoardDetailResponse& board, Login login) -> bool {
    return login && (login->local_user().admin() || login->id == board.local_board().owner());
  }
  auto InstanceController::can_change_site_settings(Login login) -> bool {
    return login && login->local_user().admin();
  }

  auto InstanceController::validate_or_regenerate_session(
    ReadTxn& txn,
    uint64_t session_id,
    string_view ip,
    string_view user_agent
  ) -> optional<LoginResponse> {
    const auto session_opt = txn.get_session(session_id);
    if (!session_opt) return {};
    const auto& session = session_opt->get();
    const auto user = session.user();
    // TODO: Change session lifetime to 1 day
    // Currently sessions regenerate every 5 minutes for testing purposes
    if (session.remember() &&  now_s() - session.created_at() >= 60 * 5) {
      auto txn = db->open_write_txn();
      auto new_session = txn.create_session(
        user,
        ip,
        user_agent,
        true,
        session.expires_at() - session.created_at()
      );
      txn.delete_session(session_id);
      txn.commit();
      return { { .user_id = user, .session_id = new_session.first, .expiration = new_session.second } };
    }
    return { { .user_id = user, .session_id = session_id, .expiration = session.expires_at() } };
  }
  auto InstanceController::login(
    string_view username_or_email,
    SecretString&& password,
    string_view ip,
    string_view user_agent,
    bool remember
  ) -> LoginResponse {
    uint8_t hash[32];
    auto txn = db->open_write_txn();
    const auto user_id_opt = username_or_email.find('@') == string_view::npos
      ? txn.get_user_id_by_name(username_or_email)
      : txn.get_user_id_by_email(username_or_email);
    if (!user_id_opt) {
      spdlog::debug("Tried to log in as nonexistent user {}", username_or_email);
      throw ControllerError("Invalid username or password", 400);
    }
    const auto user_id = *user_id_opt;
    const auto local_user = txn.get_local_user(user_id);
    if (!local_user) {
      spdlog::debug("Tried to log in as non-local user {}", username_or_email);
      throw ControllerError("Invalid username or password", 400);
    }
    hash_password(std::move(password), local_user->get().password_salt()->bytes()->Data(), hash);

    // Comment that this returns 0 on success, 1 on failure!
    if (CRYPTO_memcmp(hash, local_user->get().password_hash()->bytes()->Data(), 32)) {
      // TODO: Lock users out after repeated failures
      spdlog::debug("Tried to login with wrong password for user {}", username_or_email);
      throw ControllerError("Invalid username or password", 400);
    }
    const auto session = txn.create_session(
      user_id,
      ip,
      user_agent,
      remember,
      // "Remember me" lasts for a month, temporary sessions last for a day.
      remember ? 60 * 60 * 25 * 30 : 60 * 60 * 24
    );
    txn.commit();
    return { .user_id = user_id, .session_id = session.first, .expiration = session.second };
  }
  auto InstanceController::thread_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> ThreadDetailResponse {
    ThreadDetailResponse rsp { get_thread_entry(txn, id, login), {} };
    comment_tree(txn, rsp.comments, id, sort, login, skip_cw, rsp.thread(), rsp.hidden, rsp.board(), rsp.board_hidden, from_id);
    return rsp;
  }
  auto InstanceController::comment_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> CommentDetailResponse {
    CommentDetailResponse rsp { get_comment_entry(txn, id, login), {} };
    comment_tree(txn, rsp.comments, id, sort, login, skip_cw, rsp.thread(), rsp.thread_hidden, rsp.board(), rsp.board_hidden, from_id);
    return rsp;
  }
  auto InstanceController::user_detail(ReadTxnBase& txn, uint64_t id) -> UserDetailResponse {
    auto user = txn.get_user(id);
    auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ControllerError("User not found", 404);
    return { { id, *user, }, *user_stats };
  }
  auto InstanceController::local_user_detail(ReadTxnBase& txn, uint64_t id) -> LocalUserDetailResponse {
    const auto user = txn.get_user(id);
    const auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ControllerError("User not found", 404);
    const auto local_user = txn.get_local_user(id);
    if (!local_user) throw ControllerError("Local user not found", 404);
    return { { { id, *user }, *user_stats, }, *local_user };
  }
  auto InstanceController::board_detail(ReadTxnBase& txn, uint64_t id, optional<uint64_t> logged_in_user) -> BoardDetailResponse {
    const auto board = txn.get_board(id);
    const auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ControllerError("Board not found", 404);
    const auto subscribed = logged_in_user ? txn.is_user_subscribed_to_board(*logged_in_user, id) : false;
    return { { id, *board }, *board_stats, subscribed };
  }
  auto InstanceController::local_board_detail(ReadTxnBase& txn, uint64_t id, optional<uint64_t> logged_in_user) -> LocalBoardDetailResponse {
    const auto board = txn.get_board(id);
    const auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ControllerError("Board not found", 404);
    const auto local_board = txn.get_local_board(id);
    if (!local_board) throw ControllerError("Local board not found", 404);
    const auto subscribed = logged_in_user ? txn.is_user_subscribed_to_board(*logged_in_user, id) : false;
    return { { { id, *board }, *board_stats, subscribed }, *local_board };
  }
  auto InstanceController::list_local_users(ReadTxnBase& txn, optional<uint64_t> from_id) -> PageOf<UserListEntry> {
    PageOf<UserListEntry> out { {}, !from_id, {} };
    auto iter = txn.list_local_users(from_id ? optional(Cursor(*from_id)) : nullopt);
    for (auto id : iter) {
      const auto user = txn.get_user(id);
      if (!user) {
        spdlog::warn("Local user {:x} has no corresponding user entry (database is inconsistent!)", id);
        continue;
      }
      out.entries.push_back({ id, *user });
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  auto InstanceController::list_local_boards(ReadTxnBase& txn, optional<uint64_t> from_id) -> PageOf<BoardListEntry> {
    PageOf<BoardListEntry> out { {}, !from_id, {} };
    auto iter = txn.list_local_boards(from_id ? optional(Cursor(*from_id)) : nullopt);
    for (auto id : iter) {
      const auto board = txn.get_board(id);
      if (!board) {
        spdlog::warn("Local board {:x} has no corresponding board entry (database is inconsistent!)", id);
        continue;
      }
      out.entries.push_back({ id, *board });
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  static inline auto earliest_time(SortType sort) -> uint64_t {
    switch (sort) {
      case SortType::TopYear: return now_s() - 86400 * 365;
      case SortType::TopSixMonths: return now_s() - 86400 * 30 * 6;
      case SortType::TopThreeMonths: return now_s() - 86400 * 30 * 3;
      case SortType::TopMonth: return now_s() - 86400 * 30;
      case SortType::TopWeek: return now_s() - 86400 * 7;
      case SortType::TopDay: return now_s() - 86400;
      case SortType::TopTwelveHour: return now_s() - 3600 * 12;
      case SortType::TopSixHour: return now_s() - 3600 * 6;
      case SortType::TopHour: return now_s() - 3600;
      default: return 0;
    }
  }
  auto InstanceController::list_board_threads(
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> PageOf<ThreadListEntry> {
    PageOf<ThreadListEntry> out { {}, !from_id, {} };
    const auto board = txn.get_board(board_id);
    if (!board) {
      throw ControllerError("Board does not exist", 404);
    }
    const bool board_hidden = login && txn.has_user_hidden_board(login->id, board_id);
    switch (sort) {
      case SortType::New: {
        auto iter = txn.list_threads_of_board_new(board_id, next_cursor_thread_new(txn, board_id, from_id));
        for (uint64_t thread_id : iter) {
          const auto entry = get_thread_entry(txn, thread_id, login, {}, false, board, board_hidden);
          if (!should_show(entry, login, skip_cw)) continue;
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_2() };
        break;
      }
      case SortType::TopAll:
      case SortType::TopYear:
      case SortType::TopSixMonths:
      case SortType::TopThreeMonths:
      case SortType::TopMonth:
      case SortType::TopWeek:
      case SortType::TopDay:
      case SortType::TopTwelveHour:
      case SortType::TopSixHour:
      case SortType::TopHour: {
        auto iter = txn.list_threads_of_board_top(board_id, next_cursor_top(txn, board_id, from_id));
        const auto earliest = earliest_time(sort);
        for (uint64_t thread_id : iter) {
          const auto entry = get_thread_entry(txn, thread_id, login, {}, false, board, board_hidden);
          if (entry.thread().created_at() < earliest || !should_show(entry, login, skip_cw)) continue;
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_2() };
        break;
      }
      case SortType::Hot: {
        auto ranked = ranked_page<ThreadListEntry>(
          txn,
          txn.list_threads_of_board_new(board_id),
          txn.list_threads_of_board_top(board_id),
          login,
          skip_cw,
          [&](uint64_t id) { return get_thread_entry(txn, id, login, {}, false, board, board_hidden); },
          [&](auto& e) { return e.thread().created_at(); },
          thread_rank_cmp,
          from_id
        );
        for (auto entry : ranked.page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        break;
      }
      default:
        throw ControllerError("Sort type not yet supported");
    }
    return out;
  }
  auto InstanceController::list_board_comments(
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> PageOf<CommentListEntry> {
    PageOf<CommentListEntry> out { {}, !from_id, {} };
    const auto board = txn.get_board(board_id);
    if (!board) {
      throw ControllerError("Board does not exist", 404);
    }
    const bool board_hidden = login && txn.has_user_hidden_board(login->id, board_id);
    switch (sort) {
      case SortType::New: {
        auto iter = txn.list_comments_of_board_new(board_id, next_cursor_comment_new(txn, board_id, from_id));
        for (uint64_t comment_id : iter) {
          const auto entry = get_comment_entry(txn, comment_id, login, {}, false, {}, false, board, board_hidden);
          if (!should_show(entry, login, skip_cw)) continue;
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_2() };
        break;
      }
      case SortType::TopAll:
      case SortType::TopYear:
      case SortType::TopSixMonths:
      case SortType::TopThreeMonths:
      case SortType::TopMonth:
      case SortType::TopWeek:
      case SortType::TopDay:
      case SortType::TopTwelveHour:
      case SortType::TopSixHour:
      case SortType::TopHour: {
        auto iter = txn.list_comments_of_board_top(board_id, next_cursor_top(txn, board_id, from_id));
        const auto earliest = earliest_time(sort);
        for (uint64_t comment_id : iter) {
          const auto entry = get_comment_entry(txn, comment_id, login, {}, false, {}, false, board, board_hidden);
          if (entry.comment().created_at() < earliest || !should_show(entry, login, skip_cw)) continue;
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_2() };
        break;
      }
      case SortType::Hot: {
        auto ranked = ranked_page<CommentListEntry>(
          txn,
          txn.list_comments_of_board_new(board_id),
          txn.list_comments_of_board_top(board_id),
          login,
          skip_cw,
          [&](uint64_t id) { return get_comment_entry(txn, id, login, {}, false, {}, false, board, board_hidden); },
          [&](auto& e) { return e.comment().created_at(); },
          comment_rank_cmp,
          from_id
        );
        for (auto entry : ranked.page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        break;
      }
      default:
        throw ControllerError("Sort type not yet supported");
    }
    return out;
  }
  auto InstanceController::list_user_threads(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> PageOf<ThreadListEntry> {
    PageOf<ThreadListEntry> out { {}, !from_id, {} };
    const auto user = txn.get_user(user_id);
    if (!user) {
      throw ControllerError("User does not exist", 404);
    }
    // TODO: Old sort
    auto iter = sort == UserPostSortType::Top
      ? txn.list_threads_of_user_top(user_id, next_cursor_top(txn, user_id, from_id))
      : txn.list_threads_of_user_new(user_id, from_id ? optional(Cursor(user_id, *from_id)) : nullopt);
    for (uint64_t thread_id : iter) {
      const auto entry = get_thread_entry(txn, thread_id, login, user);
      if (!should_show(entry, login, skip_cw)) continue;
      out.entries.push_back(std::move(entry));
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = {
      sort == UserPostSortType::Top ? iter.get_cursor()->int_field_2() : iter.get_cursor()->int_field_1()
    };
    return out;
  }
  auto InstanceController::list_user_comments(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> PageOf<CommentListEntry> {
    PageOf<CommentListEntry> out { {}, !from_id, {} };
    // TODO: Old sort
    auto iter = sort == UserPostSortType::Top
      ? txn.list_comments_of_user_top(user_id, next_cursor_top(txn, user_id, from_id))
      : txn.list_comments_of_user_new(user_id, from_id ? optional(Cursor(user_id, *from_id)) : nullopt);
    for (uint64_t comment_id : iter) {
      const auto entry = get_comment_entry(txn, comment_id, login);
      if (!should_show(entry, login, skip_cw)) continue;
      out.entries.push_back(std::move(entry));
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = {
      sort == UserPostSortType::Top ? iter.get_cursor()->int_field_2() : iter.get_cursor()->int_field_1()
    };
    return out;
  }
  static auto create_local_user_internal(
    const InstanceController* controller,
    WriteTxn& txn,
    string_view username,
    optional<string_view> email,
    SecretString&& password,
    bool is_bot,
    optional<uint64_t> invite
  ) -> uint64_t {
    if (!regex_match(username.begin(), username.end(), username_regex)) {
      throw ControllerError("Invalid username (only letters, numbers, and underscores allowed; max 64 characters)", 400);
    }
    if (email && !regex_match(email->begin(), email->end(), email_regex)) {
      throw ControllerError("Invalid email address", 400);
    }
    if (password.str.length() < 8) {
      throw ControllerError("Password must be at least 8 characters", 400);
    }
    if (txn.get_user_id_by_name(username)) {
      throw ControllerError("A user with this name already exists on this instance", 409);
    }
    if (email && txn.get_user_id_by_email(*email)) {
      throw ControllerError("A user with this email address already exists on this instance", 409);
    }
    uint8_t salt[16], hash[32];
    duthomhas::csprng rng;
    rng(salt);
    controller->hash_password(std::move(password), salt, hash);
    flatbuffers::FlatBufferBuilder fbb;
    {
      const auto name_s = fbb.CreateString(username);
      UserBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_name(name_s);
      b.add_bot(is_bot);
      fbb.Finish(b.Finish());
    }
    const auto user_id = txn.create_user(fbb);
    fbb.Clear();
    {
      const auto email_s = email.transform([&](auto s) { return fbb.CreateString(s); });
      Hash hash_struct(hash);
      Salt salt_struct(salt);
      LocalUserBuilder b(fbb);
      if (email_s) b.add_email(*email_s);
      b.add_password_hash(&hash_struct);
      b.add_password_salt(&salt_struct);
      if (invite) b.add_invite(*invite);
      fbb.Finish(b.Finish());
    }
    txn.set_local_user(user_id, fbb);
    return user_id;
  }
  auto InstanceController::register_local_user(
    string_view username,
    string_view email,
    SecretString&& password,
    string_view ip,
    string_view user_agent,
    optional<uint64_t> invite_id,
    optional<string_view> application_text
  ) -> std::pair<uint64_t, bool> {
    const auto site = site_detail();
    if (!site->registration_enabled) {
      throw ControllerError("Registration is not allowed on this server", 403);
    }
    if (site->registration_application_required && !application_text) {
      throw ControllerError("An application reason is required to register", 400);
    }
    if (site->registration_invite_required) {
      if (!invite_id) {
        throw ControllerError("An invite code is required to register", 400);
      }
    }
    auto txn = db->open_write_txn();
    const auto user_id = create_local_user_internal(
      this, txn, username, email, std::move(password), false, invite_id
    );
    if (invite_id) {
      const auto invite_opt = txn.get_invite(*invite_id);
      if (!invite_opt) {
        throw ControllerError("Invalid invite code", 400);
      }
      const auto& invite = invite_opt->get();
      if (invite.accepted_at()) {
        spdlog::warn("Attempt to use already-used invite code {} (for username {}, email {}, ip {}, user agent {})",
          invite_id_to_code(*invite_id), username, email, ip, user_agent
        );
        throw ControllerError("Expired invite code", 400);
      }
      const auto now = now_s();
      if (invite.expires_at() <= now) {
        throw ControllerError("Expired invite code", 400);
      }
      FlatBufferBuilder fbb;
      InviteBuilder b(fbb);
      b.add_from(invite.from());
      b.add_to(user_id);
      b.add_created_at(invite.created_at());
      b.add_accepted_at(now);
      b.add_expires_at(invite.expires_at());
      fbb.Finish(b.Finish());
      txn.set_invite(*invite_id, fbb);
    }
    if (site->registration_application_required) {
      FlatBufferBuilder fbb;
      auto ip_s = fbb.CreateString(ip),
           user_agent_s = fbb.CreateString(user_agent),
           application_text_s = fbb.CreateString(*application_text);
      ApplicationBuilder b(fbb);
      b.add_ip(ip_s);
      b.add_user_agent(user_agent_s);
      b.add_text(application_text_s);
      fbb.Finish(b.Finish());
      txn.create_application(user_id, fbb);
    }
    txn.commit();
    return { user_id, site->registration_application_required };
  }
  auto InstanceController::create_local_user(
    string_view username,
    optional<string_view> email,
    SecretString&& password,
    bool is_bot,
    optional<uint64_t> invite
  ) -> uint64_t {
    auto txn = db->open_write_txn();
    auto user_id = create_local_user_internal(
      this, txn, username, email, std::move(password), is_bot, invite
    );
    txn.commit();
    return user_id;
  }
  auto InstanceController::create_local_board(
    uint64_t owner,
    string_view name,
    optional<string_view> display_name,
    optional<string_view> content_warning,
    bool is_private,
    bool is_restricted_posting,
    bool is_local_only
  ) -> uint64_t {
    if (!regex_match(name.begin(), name.end(), username_regex)) {
      throw ControllerError("Invalid board name (only letters, numbers, and underscores allowed; max 64 characters)", 400);
    }
    if (display_name && display_name->length() > 1024) {
      throw ControllerError("Display name cannot be longer than 1024 bytes", 400);
    }
    auto txn = db->open_write_txn();
    if (txn.get_board_id_by_name(name)) {
      throw ControllerError("A board with this name already exists on this instance", 409);
    }
    if (!txn.get_local_user(owner)) {
      throw ControllerError("Board owner is not a user on this instance", 400);
    }
    // TODO: Check if user is allowed to create boards
    flatbuffers::FlatBufferBuilder fbb;
    {
      const auto display_name_s = display_name.transform([&](auto s) { return fbb.CreateString(s); }),
        content_warning_s = content_warning.transform([&](auto s) { return fbb.CreateString(s); });
      BoardBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_name(fbb.CreateString(name));
      if (display_name) b.add_display_name(*display_name_s);
      if (content_warning) b.add_content_warning(*content_warning_s);
      b.add_restricted_posting(is_restricted_posting);
      fbb.Finish(b.Finish());
    }
    const auto board_id = txn.create_board(fbb);
    fbb.Clear();
    {
      LocalBoardBuilder b(fbb);
      b.add_owner(owner);
      b.add_private_(is_private);
      b.add_federated(!is_local_only);
      fbb.Finish(b.Finish());
    }
    txn.set_local_board(board_id, fbb);
    txn.commit();
    return board_id;
  }
  auto InstanceController::create_local_thread(
    uint64_t author,
    uint64_t board,
    string_view title,
    optional<string_view> submission_url,
    optional<string_view> text_content_markdown,
    optional<string_view> content_warning
  ) -> uint64_t {
    if (submission_url) {
      auto len = submission_url->length();
      if (len > 2048) {
        throw ControllerError("Submission URL cannot be longer than 2048 bytes", 400);
      } else if (len < 1) {
        submission_url = {};
      }
    }
    if (text_content_markdown) {
      auto len = text_content_markdown->length();
      if (len > 1024 * 1024) {
        throw ControllerError("Post text content cannot be larger than 1MB", 400);
      } else if (len < 1) {
        text_content_markdown = {};
      }
    }
    if (!submission_url && !text_content_markdown) {
      throw ControllerError("Post must contain either a submission URL or text content", 400);
    }
    auto len = title.length();
    if (len > 1024) {
      throw ControllerError("Post title cannot be longer than 1024 bytes", 400);
    } else if (len < 1) {
      throw ControllerError("Post title cannot be blank", 400);
    }
    uint64_t thread_id;
    {
      auto txn = db->open_write_txn();
      if (!txn.get_local_user(author)) {
        throw ControllerError("Post author is not a user on this instance", 400);
      }
      if (!txn.get_board(board)) {
        throw ControllerError("Board does not exist", 400);
      }
      // TODO: Check if user is banned
      flatbuffers::FlatBufferBuilder fbb;
      const auto title_s = fbb.CreateString(title),
        submission_s = submission_url ? fbb.CreateString(*submission_url) : 0,
        content_raw_s = text_content_markdown ? fbb.CreateString(*text_content_markdown) : 0,
        content_safe_s = text_content_markdown ? fbb.CreateString(escape_html(*text_content_markdown)) : 0,
        content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0;
      ThreadBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_author(author);
      b.add_board(board);
      b.add_title(title_s);
      if (submission_url) b.add_content_url(submission_s);
      if (text_content_markdown) {
        // TODO: Parse Markdown and HTML
        b.add_content_text_raw(content_raw_s);
        b.add_content_text_safe(content_safe_s);
      }
      if (content_warning) b.add_content_warning(content_warning_s);
      fbb.Finish(b.Finish());
      thread_id = txn.create_thread(fbb);
      txn.set_vote(author, thread_id, Vote::Upvote);
      txn.commit();
    }
    dispatch_event(Event::UserStatsUpdate, author);
    dispatch_event(Event::BoardStatsUpdate, board);
    return thread_id;
  }
  auto InstanceController::create_local_comment(
    uint64_t author,
    uint64_t parent,
    string_view text_content_markdown,
    optional<string_view> content_warning
  ) -> uint64_t {
    auto len = text_content_markdown.length();
    if (len > 1024 * 1024) {
      throw ControllerError("Comment text content cannot be larger than 1MB", 400);
    } else if (len < 1) {
      throw ControllerError("Comment text content cannot be blank", 400);
    }
    uint64_t comment_id, thread_id, board_id;
    {
      auto txn = db->open_write_txn();
      if (!txn.get_local_user(author)) {
        throw ControllerError("Comment author is not a user on this instance", 400);
      }
      auto parent_thread = txn.get_thread(parent);
      const auto parent_comment = !parent_thread ? txn.get_comment(parent) : nullopt;
      if (parent_comment) parent_thread = txn.get_thread(parent_comment->get().thread());
      if (!parent_thread) {
        throw ControllerError("Comment parent post does not exist", 400);
      }
      board_id = parent_thread->get().board();
      // TODO: Check if user is banned
      flatbuffers::FlatBufferBuilder fbb;
      const auto content_raw_s = fbb.CreateString(text_content_markdown),
        content_safe_s = fbb.CreateString(escape_html(text_content_markdown)),
        content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0;
      CommentBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_author(author);
      b.add_thread(thread_id = parent_comment ? parent_comment->get().thread() : parent);
      b.add_parent(parent);
      // TODO: Parse Markdown and HTML
      b.add_content_raw(content_raw_s);
      b.add_content_safe(content_safe_s);
      if (content_warning) b.add_content_warning(content_warning_s);
      fbb.Finish(b.Finish());
      comment_id = txn.create_comment(fbb);
      txn.set_vote(author, comment_id, Vote::Upvote);
      txn.commit();
    }
    dispatch_event(Event::UserStatsUpdate, author);
    dispatch_event(Event::BoardStatsUpdate, board_id);
    dispatch_event(Event::PageStatsUpdate, thread_id);
    if (parent != thread_id) dispatch_event(Event::CommentStatsUpdate, parent);
    return comment_id;
  }
  auto InstanceController::vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_user(user_id)) {
      throw ControllerError("User does not exist", 400);
    }
    const auto thread = txn.get_thread(post_id);
    const auto comment = !thread ? txn.get_comment(post_id) : nullopt;
    if (!thread && !comment) {
      throw ControllerError("Post does not exist", 400);
    }
    const auto op = thread ? thread->get().author() : comment->get().author();
    txn.set_vote(user_id, post_id, vote);
    txn.commit();

    dispatch_event(Event::UserStatsUpdate, op);
    if (thread) dispatch_event(Event::PageStatsUpdate, post_id);
    if (comment) dispatch_event(Event::CommentStatsUpdate, post_id);
  }
  auto InstanceController::subscribe(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_user(user_id)) {
      throw ControllerError("User does not exist", 400);
    }
    if (!txn.get_board(board_id)) {
      throw ControllerError("Board does not exist", 400);
    }
    txn.set_subscription(user_id, board_id, subscribed);
    txn.commit();

    dispatch_event(Event::UserStatsUpdate, user_id);
    dispatch_event(Event::BoardStatsUpdate, board_id);
  }
  auto InstanceController::save_post(uint64_t user_id, uint64_t post_id, bool saved) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) {
      throw ControllerError("User does not exist", 400);
    }
    if (!txn.get_post_stats(post_id)) {
      throw ControllerError("Post does not exist", 400);
    }
    txn.set_save(user_id, post_id, saved);
    txn.commit();
  }
  auto InstanceController::hide_post(uint64_t user_id, uint64_t post_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) {
      throw ControllerError("User does not exist", 400);
    }
    if (!txn.get_post_stats(post_id)) {
      throw ControllerError("Post does not exist", 400);
    }
    txn.set_hide_post(user_id, post_id, hidden);
    txn.commit();
  }
  auto InstanceController::hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id) || !txn.get_user(hidden_user_id)) {
      throw ControllerError("User does not exist", 400);
    }
    txn.set_hide_user(user_id, hidden_user_id, hidden);
    txn.commit();
  }
  auto InstanceController::hide_board(uint64_t user_id, uint64_t board_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) {
      throw ControllerError("User does not exist", 400);
    }
    if (!txn.get_post_stats(board_id)) {
      throw ControllerError("Board does not exist", 400);
    }
    txn.set_hide_post(user_id, board_id, hidden);
    txn.commit();
  }
}
