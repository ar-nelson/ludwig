#include "instance.h++"
#include "util/web.h++"
#include <mutex>
#include <regex>
#include <duthomhas/csprng.hpp>
#include <openssl/evp.h>

using std::function, std::nullopt, std::regex, std::regex_match, std::optional,
      std::shared_ptr, std::string, std::string_view, flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot;

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
  template <typename T> static auto rank_cmp(const T& a, const T& b) -> bool {
    return a.rank > b.rank || (a.rank == b.rank && a.id > b.id);
  }
  template <typename T> static auto latest_comment_cmp(const T& a, const T& b) -> bool {
    return a.stats().latest_comment() > b.stats().latest_comment();
  }
  static inline auto next_cursor_new(uint64_t prefix, optional<uint64_t> from) -> optional<Cursor> {
    if (!from) return {};
    return Cursor(prefix, *from - 1, ID_MAX);
  }
  static inline auto next_cursor_old(uint64_t prefix, optional<uint64_t> from) -> optional<Cursor> {
    if (!from) return {};
    return Cursor(prefix, *from + 1, 0);
  }
  static inline auto next_cursor_top(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto stats = txn.get_post_stats(*from_id);
    if (!stats) return Cursor(prefix, 0, 0);
    return Cursor(prefix, karma_uint(stats->get().karma()), (*from_id) - 1);
  }
  static inline auto next_cursor_new_comments(uint64_t prefix, optional<uint64_t> from) -> optional<Cursor> {
    if (!from) return {};
    return Cursor(prefix, *from + ACTIVE_COMMENT_MAX_AGE, ID_MAX);
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
      throw ApiError("Database error", 500);
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
      throw ApiError("Database error", 500);
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

  template <typename T> struct RankedPage {
    std::set<T, decltype(rank_cmp<T>)*> page;
    optional<uint64_t> next;
  };
  template <typename T> static auto ranked_page(
    ReadTxnBase& txn,
    DBIter<uint64_t> iter_by_new,
    DBIter<uint64_t> iter_by_top,
    InstanceController::Login login,
    function<T (uint64_t)> get_entry,
    function<uint64_t (const T&)> get_timestamp,
    optional<function<uint64_t (const T&)>> get_latest_possible_timestamp = {},
    optional<uint64_t> from = {},
    size_t page_size = ITEMS_PER_PAGE
  ) -> RankedPage<T> {
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
    std::set<T, decltype(rank_cmp<T>)*> sorted_entries(rank_cmp<T>);
    // TODO: Make this more performant by iterating pairs of <id, timestamp>
    for (auto id : iter_by_new) {
      try {
        auto entry = get_entry(id);
        if (!InstanceController::should_show(entry, login)) continue;
        const auto timestamp = get_timestamp(entry);
        const auto denominator = rank_denominator(timestamp < now ? now - timestamp : 0);
        entry.rank = rank_numerator(entry.stats().karma()) / denominator;
        if (entry.rank >= max_rank) continue;
        if (sorted_entries.size() > page_size) {
          skipped_any = true;
          double max_possible_rank;
          if (get_latest_possible_timestamp) {
            const auto latest_possible_timestamp = (*get_latest_possible_timestamp)(entry);
            const double min_possible_denominator =
              rank_denominator(latest_possible_timestamp < now ? now - latest_possible_timestamp : 0);
            max_possible_rank = max_possible_numerator / min_possible_denominator;
          } else max_possible_rank = max_possible_numerator / denominator;
          auto last = std::prev(sorted_entries.end());
          if (max_possible_rank <= last->rank) break;
          sorted_entries.erase(last);
        }
        sorted_entries.insert(entry);
      } catch (ApiError e) {
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
    OptRef<Thread> thread = {},
    bool is_thread_hidden = false,
    OptRef<Board> board = {},
    bool is_board_hidden = false,
    optional<uint64_t> from = {},
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
          [&](uint64_t id) { return InstanceController::get_comment_entry(txn, id, login, {}, false, thread, is_thread_hidden, board, is_board_hidden); },
          [&](auto& e) { return e.comment().created_at(); },
          {},
          from,
          max_comments - tree.size()
        );
        for (auto entry : ranked.page) {
          if (tree.size() >= max_comments) {
            tree.mark_continued(parent, entry.id);
            return;
          }
          const auto id = entry.id, children = entry.stats().child_count();
          tree.emplace(parent, entry);
          if (children) comment_tree(txn, tree, id, sort, login, thread, is_thread_hidden, board, is_board_hidden, {}, max_comments, max_depth - 1);
        }
        if (ranked.next) tree.mark_continued(parent, *ranked.next);
        return;
      }
      case CommentSortType::New:
        iter.emplace(txn.list_comments_of_post_new(parent, next_cursor_new(parent, from)));
        break;
      case CommentSortType::Old:
        iter.emplace(txn.list_comments_of_post_old(parent, next_cursor_old(parent, from)));
        break;
      case CommentSortType::Top:
        iter.emplace(txn.list_comments_of_post_top(parent, next_cursor_top(txn, parent, from)));
        break;
    }
    assert(!!iter);
    for (auto id : *iter) {
      if (tree.size() >= max_comments) {
        tree.mark_continued(parent, id);
        return;
      }
      auto entry = InstanceController::get_comment_entry(txn, id, login, {}, false, thread, is_thread_hidden, board, is_board_hidden);
      if (!InstanceController::should_show(entry, login)) continue;
      const auto children = entry.stats().child_count();
      tree.emplace(parent, entry);
      if (children) comment_tree(txn, tree, id, sort, login, thread, is_thread_hidden, board, is_board_hidden, {}, max_comments, max_depth - 1);
    }
    if (!iter->is_done()) {
      tree.mark_continued(parent, sort == CommentSortType::Top ? iter->get_cursor()->int_field_2() : iter->get_cursor()->int_field_1());
    }
  }

  static inline auto expect_post_stats(ReadTxnBase& txn, uint64_t post_id) -> const PostStats& {
    const auto stats = txn.get_post_stats(post_id);
    if (!stats) {
      spdlog::error("Post {:x} has no corresponding post_stats (database is inconsistent!)", post_id);
      throw ApiError("Database error");
    }
    return *stats;
  }

  InstanceController::InstanceController(
    shared_ptr<DB> db,
    shared_ptr<HttpClient> http_client,
    optional<shared_ptr<SearchEngine>> search_engine
  ) : db(db), http_client(http_client), search_engine(search_engine) {
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
      throw ApiError("Invalid username or password", 400);
    }
    const auto user_id = *user_id_opt;
    const auto local_user = txn.get_local_user(user_id);
    if (!local_user) {
      spdlog::debug("Tried to log in as non-local user {}", username_or_email);
      throw ApiError("Invalid username or password", 400);
    }
    hash_password(std::move(password), local_user->get().password_salt()->bytes()->Data(), hash);

    // Comment that this returns 0 on success, 1 on failure!
    if (CRYPTO_memcmp(hash, local_user->get().password_hash()->bytes()->Data(), 32)) {
      // TODO: Lock users out after repeated failures
      spdlog::debug("Tried to login with wrong password for user {}", username_or_email);
      throw ApiError("Invalid username or password", 400);
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
    optional<uint64_t> from
  ) -> ThreadDetailResponse {
    ThreadDetailResponse rsp { get_thread_entry(txn, id, login), {} };
    comment_tree(txn, rsp.comments, id, sort, login, rsp.thread(), rsp.hidden, rsp.board(), rsp.board_hidden, from);
    return rsp;
  }
  auto InstanceController::comment_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    optional<uint64_t> from
  ) -> CommentDetailResponse {
    CommentDetailResponse rsp { get_comment_entry(txn, id, login), {} };
    comment_tree(txn, rsp.comments, id, sort, login, rsp.thread(), rsp.thread_hidden, rsp.board(), rsp.board_hidden, from);
    return rsp;
  }
  auto InstanceController::user_detail(ReadTxnBase& txn, uint64_t id) -> UserDetailResponse {
    auto user = txn.get_user(id);
    auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ApiError("User not found", 404);
    return { { id, *user, }, *user_stats };
  }
  auto InstanceController::local_user_detail(ReadTxnBase& txn, uint64_t id) -> LocalUserDetailResponse {
    const auto user = txn.get_user(id);
    const auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ApiError("User not found", 404);
    const auto local_user = txn.get_local_user(id);
    if (!local_user) throw ApiError("Local user not found", 404);
    return { { { id, *user }, *user_stats, }, *local_user };
  }
  auto InstanceController::board_detail(ReadTxnBase& txn, uint64_t id, optional<uint64_t> logged_in_user) -> BoardDetailResponse {
    const auto board = txn.get_board(id);
    const auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ApiError("Board not found", 404);
    const auto subscribed = logged_in_user ? txn.is_user_subscribed_to_board(*logged_in_user, id) : false;
    return { { id, *board }, *board_stats, subscribed };
  }
  auto InstanceController::local_board_detail(ReadTxnBase& txn, uint64_t id, optional<uint64_t> logged_in_user) -> LocalBoardDetailResponse {
    const auto board = txn.get_board(id);
    const auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ApiError("Board not found", 404);
    const auto local_board = txn.get_local_board(id);
    if (!local_board) throw ApiError("Local board not found", 404);
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
    if (!iter.is_done()) out.next = iter.get_cursor()->int_field_0();
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
    if (!iter.is_done()) out.next = iter.get_cursor()->int_field_0();
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
    optional<uint64_t> from
  ) -> PageOf<ThreadListEntry> {
    PageOf<ThreadListEntry> out { {}, !from, {} };
    const auto board = txn.get_board(board_id);
    if (!board) throw ApiError("Board does not exist", 404);
    optional<DBIter<uint64_t>> iter;
    switch (sort) {
      case SortType::Active: {
        const auto now = now_s();
        auto ranked = ranked_page<ThreadListEntry>(
          txn,
          txn.list_threads_of_board_new(board_id),
          txn.list_threads_of_board_top(board_id),
          login,
          [&](uint64_t id) { return get_thread_entry(txn, id, login, {}, false, board, false); },
          [&](auto& e) { return e.stats().latest_comment(); },
          [&](auto& e) { return std::min(now, e.thread().created_at() + ACTIVE_COMMENT_MAX_AGE); },
          from
        );
        for (auto entry : ranked.page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        return out;
      }
      case SortType::Hot: {
        auto ranked = ranked_page<ThreadListEntry>(
          txn,
          txn.list_threads_of_board_new(board_id),
          txn.list_threads_of_board_top(board_id),
          login,
          [&](uint64_t id) { return get_thread_entry(txn, id, login, {}, false, board, false); },
          [&](auto& e) { return e.thread().created_at(); },
          {},
          from
        );
        for (auto entry : ranked.page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        return out;
      }
      case SortType::NewComments: {
        std::set<ThreadListEntry, decltype(latest_comment_cmp<ThreadListEntry>)*> page(latest_comment_cmp);
        bool has_more;
        for (uint64_t thread_id : txn.list_threads_of_board_new(board_id, next_cursor_new_comments(board_id, from))) {
          const auto entry = get_thread_entry(txn, thread_id, login, {}, false, board, false);
          if (from && entry.stats().latest_comment() > from) continue;
          const bool full = has_more = page.size() >= ITEMS_PER_PAGE;
          if (full) {
            const auto last = std::prev(page.end())->stats().latest_comment();
            if (entry.stats().latest_comment() + ACTIVE_COMMENT_MAX_AGE < last) break;
          }
          if (!should_show(entry, login)) continue;
          page.emplace(entry);
          if (full) page.erase(std::prev(page.end()));
        }
        for (auto entry : page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        if (has_more) out.next = std::prev(page.end())->stats().latest_comment();
        return out;
      }
      case SortType::New:
        iter.emplace(txn.list_threads_of_board_new(board_id, next_cursor_new(board_id, from)));
        break;
      case SortType::Old:
        iter.emplace(txn.list_threads_of_board_old(board_id, next_cursor_old(board_id, from)));
        break;
      case SortType::MostComments:
        iter.emplace(txn.list_threads_of_board_most_comments(board_id, next_cursor_most_comments(txn, board_id, from)));
        break;
      case SortType::TopAll:
      case SortType::TopYear:
      case SortType::TopSixMonths:
      case SortType::TopThreeMonths:
      case SortType::TopMonth:
      case SortType::TopWeek:
      case SortType::TopDay:
      case SortType::TopTwelveHour:
      case SortType::TopSixHour:
      case SortType::TopHour:
        iter.emplace(txn.list_threads_of_board_top(board_id, next_cursor_top(txn, board_id, from)));
        break;
    }
    assert(!!iter);
    const auto earliest = earliest_time(sort);
    for (uint64_t thread_id : *iter) {
      const auto entry = get_thread_entry(txn, thread_id, login, {}, false, board, false);
      if (entry.thread().created_at() < earliest || !should_show(entry, login)) continue;
      out.entries.push_back(std::move(entry));
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) {
      if (sort == SortType::New || sort == SortType::Old) {
        out.next = iter->get_cursor()->int_field_1();
      } else {
        out.next = iter->get_cursor()->int_field_2();
      }
    }
    return out;
  }
  auto InstanceController::list_board_comments(
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    optional<uint64_t> from
  ) -> PageOf<CommentListEntry> {
    PageOf<CommentListEntry> out { {}, !from, {} };
    const auto board = txn.get_board(board_id);
    if (!board) throw ApiError("Board does not exist", 404);
    optional<DBIter<uint64_t>> iter;
    switch (sort) {
      case SortType::Active: {
        const auto now = now_s();
        auto ranked = ranked_page<CommentListEntry>(
          txn,
          txn.list_comments_of_board_new(board_id),
          txn.list_comments_of_board_top(board_id),
          login,
          [&](uint64_t id) { return get_comment_entry(txn, id, login, {}, false, {}, false, board, false); },
          [&](auto& e) { return e.stats().latest_comment(); },
          [&](auto& e) { return std::min(now, e.comment().created_at() + ACTIVE_COMMENT_MAX_AGE); },
          from
        );
        for (auto entry : ranked.page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        return out;
      }
      case SortType::Hot: {
        auto ranked = ranked_page<CommentListEntry>(
          txn,
          txn.list_comments_of_board_new(board_id),
          txn.list_comments_of_board_top(board_id),
          login,
          [&](uint64_t id) { return get_comment_entry(txn, id, login, {}, false, {}, false, board, false); },
          [&](auto& e) { return e.comment().created_at(); },
          {},
          from
        );
        for (auto entry : ranked.page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        return out;
      }
      case SortType::NewComments: {
        std::set<CommentListEntry, decltype(latest_comment_cmp<CommentListEntry>)*> page(latest_comment_cmp);
        bool has_more;
        for (uint64_t comment_id : txn.list_comments_of_board_new(board_id, next_cursor_new_comments(board_id, from))) {
          const auto entry = get_comment_entry(txn, comment_id, login, {}, false, {}, false, board, false);
          if (from && entry.stats().latest_comment() > from) continue;
          const bool full = has_more = page.size() >= ITEMS_PER_PAGE;
          if (full) {
            const auto last = std::prev(page.end())->stats().latest_comment();
            if (entry.stats().latest_comment() + ACTIVE_COMMENT_MAX_AGE < last) break;
          }
          if (!should_show(entry, login)) continue;
          page.emplace(entry);
          if (full) page.erase(std::prev(page.end()));
        }
        for (auto entry : page) {
          out.entries.push_back(std::move(entry));
          if (out.entries.size() >= ITEMS_PER_PAGE) break;
        }
        if (has_more) out.next = std::prev(page.end())->stats().latest_comment();
        return out;
      }
      case SortType::New:
        iter.emplace(txn.list_comments_of_board_new(board_id, next_cursor_new(board_id, from)));
        break;
      case SortType::Old:
        iter.emplace(txn.list_comments_of_board_old(board_id, next_cursor_old(board_id, from)));
        break;
      case SortType::MostComments:
        iter.emplace(txn.list_comments_of_board_most_comments(board_id, next_cursor_most_comments(txn, board_id, from)));
        break;
      case SortType::TopAll:
      case SortType::TopYear:
      case SortType::TopSixMonths:
      case SortType::TopThreeMonths:
      case SortType::TopMonth:
      case SortType::TopWeek:
      case SortType::TopDay:
      case SortType::TopTwelveHour:
      case SortType::TopSixHour:
      case SortType::TopHour:
        iter.emplace(txn.list_comments_of_board_top(board_id, next_cursor_top(txn, board_id, from)));
        break;
    }
    assert(!!iter);
    const auto earliest = earliest_time(sort);
    for (uint64_t comment_id : *iter) {
      const auto entry = get_comment_entry(txn, comment_id, login, {}, false, {}, false, board, false);
      if (entry.comment().created_at() < earliest || !should_show(entry, login)) continue;
      out.entries.push_back(std::move(entry));
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) {
      if (sort == SortType::New || sort == SortType::Old) {
        out.next = iter->get_cursor()->int_field_1();
      } else {
        out.next = iter->get_cursor()->int_field_2();
      }
    }
    return out;
  }
  auto InstanceController::list_user_threads(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    optional<uint64_t> from
  ) -> PageOf<ThreadListEntry> {
    PageOf<ThreadListEntry> out { {}, !from, {} };
    const auto user = txn.get_user(user_id);
    if (!user) throw ApiError("User does not exist", 404);
    optional<DBIter<uint64_t>> iter;
    switch (sort) {
      case UserPostSortType::New:
        iter.emplace(txn.list_threads_of_user_new(user_id, from.transform([&](auto i){return Cursor(user_id, i);})));
        break;
      case UserPostSortType::Old:
        iter.emplace(txn.list_threads_of_user_old(user_id, from.transform([&](auto i){return Cursor(user_id, i);})));
        break;
      case UserPostSortType::Top:
        iter.emplace(txn.list_threads_of_user_top(user_id, next_cursor_top(txn, user_id, from)));
        break;
    }
    assert(!!iter);
    for (uint64_t thread_id : *iter) {
      const auto entry = get_thread_entry(txn, thread_id, login, user);
      if (!should_show(entry, login)) continue;
      out.entries.push_back(std::move(entry));
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) out.next = {
      sort == UserPostSortType::Top ? iter->get_cursor()->int_field_2() : iter->get_cursor()->int_field_1()
    };
    return out;
  }
  auto InstanceController::list_user_comments(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    optional<uint64_t> from
  ) -> PageOf<CommentListEntry> {
    PageOf<CommentListEntry> out { {}, !from, {} };
    optional<DBIter<uint64_t>> iter;
    switch (sort) {
      case UserPostSortType::New:
        iter.emplace(txn.list_comments_of_user_new(user_id, from.transform([&](auto i){return Cursor(user_id, i);})));
        break;
      case UserPostSortType::Old:
        iter.emplace(txn.list_comments_of_user_old(user_id, from.transform([&](auto i){return Cursor(user_id, i);})));
        break;
      case UserPostSortType::Top:
        iter.emplace(txn.list_comments_of_user_top(user_id, next_cursor_top(txn, user_id, from)));
        break;
    }
    assert(!!iter);
    for (uint64_t comment_id : *iter) {
      const auto entry = get_comment_entry(txn, comment_id, login);
      if (!should_show(entry, login)) continue;
      out.entries.push_back(std::move(entry));
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) out.next = {
      sort == UserPostSortType::Top ? iter->get_cursor()->int_field_2() : iter->get_cursor()->int_field_1()
    };
    return out;
  }
  auto InstanceController::search(
    SearchQuery query,
    Login login,
    size_t offset,
    SearchCallback callback
  ) -> void {
    if (!search_engine) throw ApiError("Search is not enabled on this server", 403);
    const auto limit = query.limit ? query.limit : ITEMS_PER_PAGE;
    query.limit = limit + offset;
    (*search_engine)->search(query, [self = this->shared_from_this(), offset, limit, login, callback = std::move(callback)](auto results) mutable {
      auto txn = self->open_read_txn();
      std::vector<SearchResultListEntry> entries;
      entries.reserve(limit);
      for (size_t i = offset; i < results.size(); i++) {
        const auto id = results[i].id;
        switch (results[i].type) {
        case SearchResultType::User:
          if (auto user = txn.get_user(id)) entries.emplace_back(UserListEntry{id, *user});
          break;
        case SearchResultType::Board:
          if (auto board = txn.get_board(id)) entries.emplace_back(BoardListEntry{id, *board});
          break;
        case SearchResultType::Thread:
          try {
            entries.emplace_back(get_thread_entry(txn, id, login));
          } catch (...) {}
          break;
        case SearchResultType::Comment:
          try {
            entries.emplace_back(get_comment_entry(txn, id, login));
          } catch (...) {}
          break;
        }
      }
      callback(txn, entries);
    });
  }


  auto InstanceController::create_local_user_internal(
    WriteTxn& txn,
    string_view username,
    optional<string_view> email,
    SecretString&& password,
    bool is_bot,
    optional<uint64_t> invite
  ) -> uint64_t {
    if (!regex_match(username.begin(), username.end(), username_regex)) {
      throw ApiError("Invalid username (only letters, numbers, and underscores allowed; max 64 characters)", 400);
    }
    if (email && !regex_match(email->begin(), email->end(), email_regex)) {
      throw ApiError("Invalid email address", 400);
    }
    if (password.str.length() < 8) {
      throw ApiError("Password must be at least 8 characters", 400);
    }
    if (txn.get_user_id_by_name(username)) {
      throw ApiError("A user with this name already exists on this instance", 409);
    }
    if (email && txn.get_user_id_by_email(*email)) {
      throw ApiError("A user with this email address already exists on this instance", 409);
    }
    uint8_t salt[16], hash[32];
    duthomhas::csprng rng;
    rng(salt);
    hash_password(std::move(password), salt, hash);
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
    if (search_engine) {
      (*search_engine)->index(user_id, *GetRoot<User>(fbb.GetBufferPointer()));
    }
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
      throw ApiError("Registration is not allowed on this server", 403);
    }
    if (site->registration_application_required && !application_text) {
      throw ApiError("An application reason is required to register", 400);
    }
    if (site->registration_invite_required) {
      if (!invite_id) {
        throw ApiError("An invite code is required to register", 400);
      }
    }
    auto txn = db->open_write_txn();
    const auto user_id = create_local_user_internal(
      txn, username, email, std::move(password), false, invite_id
    );
    if (invite_id) {
      const auto invite_opt = txn.get_invite(*invite_id);
      if (!invite_opt) {
        throw ApiError("Invalid invite code", 400);
      }
      const auto& invite = invite_opt->get();
      if (invite.accepted_at()) {
        spdlog::warn("Attempt to use already-used invite code {} (for username {}, email {}, ip {}, user agent {})",
          invite_id_to_code(*invite_id), username, email, ip, user_agent
        );
        throw ApiError("Expired invite code", 400);
      }
      const auto now = now_s();
      if (invite.expires_at() <= now) {
        throw ApiError("Expired invite code", 400);
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
      txn, username, email, std::move(password), is_bot, invite
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
      throw ApiError("Invalid board name (only letters, numbers, and underscores allowed; max 64 characters)", 400);
    }
    if (display_name && display_name->length() > 1024) {
      throw ApiError("Display name cannot be longer than 1024 bytes", 400);
    }
    auto txn = db->open_write_txn();
    if (txn.get_board_id_by_name(name)) {
      throw ApiError("A board with this name already exists on this instance", 409);
    }
    if (!txn.get_local_user(owner)) {
      throw ApiError("Board owner is not a user on this instance", 400);
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
    if (search_engine) {
      (*search_engine)->index(board_id, *GetRoot<Board>(fbb.GetBufferPointer()));
    }
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
        throw ApiError("Submission URL cannot be longer than 2048 bytes", 400);
      } else if (len < 1) {
        submission_url = {};
      }
    }
    if (text_content_markdown) {
      auto len = text_content_markdown->length();
      if (len > 1024 * 1024) {
        throw ApiError("Post text content cannot be larger than 1MB", 400);
      } else if (len < 1) {
        text_content_markdown = {};
      }
    }
    if (!submission_url && !text_content_markdown) {
      throw ApiError("Post must contain either a submission URL or text content", 400);
    }
    auto len = title.length();
    if (len > 1024) {
      throw ApiError("Post title cannot be longer than 1024 bytes", 400);
    } else if (len < 1) {
      throw ApiError("Post title cannot be blank", 400);
    }
    uint64_t thread_id;
    {
      auto txn = db->open_write_txn();
      if (!txn.get_local_user(author)) {
        throw ApiError("Post author is not a user on this instance", 400);
      }
      if (!txn.get_board(board)) {
        throw ApiError("Board does not exist", 400);
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
      if (search_engine) {
        (*search_engine)->index(thread_id, *GetRoot<Thread>(fbb.GetBufferPointer()));
      }
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
      throw ApiError("Comment text content cannot be larger than 1MB", 400);
    } else if (len < 1) {
      throw ApiError("Comment text content cannot be blank", 400);
    }
    uint64_t comment_id, thread_id, board_id;
    {
      auto txn = db->open_write_txn();
      if (!txn.get_local_user(author)) {
        throw ApiError("Comment author is not a user on this instance", 400);
      }
      auto parent_thread = txn.get_thread(parent);
      const auto parent_comment = !parent_thread ? txn.get_comment(parent) : nullopt;
      if (parent_comment) parent_thread = txn.get_thread(parent_comment->get().thread());
      if (!parent_thread) {
        throw ApiError("Comment parent post does not exist", 400);
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
      if (search_engine) {
        (*search_engine)->index(comment_id, *GetRoot<Comment>(fbb.GetBufferPointer()));
      }
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
      throw ApiError("User does not exist", 400);
    }
    const auto thread = txn.get_thread(post_id);
    const auto comment = !thread ? txn.get_comment(post_id) : nullopt;
    if (!thread && !comment) {
      throw ApiError("Post does not exist", 400);
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
      throw ApiError("User does not exist", 400);
    }
    if (!txn.get_board(board_id)) {
      throw ApiError("Board does not exist", 400);
    }
    txn.set_subscription(user_id, board_id, subscribed);
    txn.commit();

    dispatch_event(Event::UserStatsUpdate, user_id);
    dispatch_event(Event::BoardStatsUpdate, board_id);
  }
  auto InstanceController::save_post(uint64_t user_id, uint64_t post_id, bool saved) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) {
      throw ApiError("User does not exist", 400);
    }
    if (!txn.get_post_stats(post_id)) {
      throw ApiError("Post does not exist", 400);
    }
    txn.set_save(user_id, post_id, saved);
    txn.commit();
  }
  auto InstanceController::hide_post(uint64_t user_id, uint64_t post_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) {
      throw ApiError("User does not exist", 400);
    }
    if (!txn.get_post_stats(post_id)) {
      throw ApiError("Post does not exist", 400);
    }
    txn.set_hide_post(user_id, post_id, hidden);
    txn.commit();
  }
  auto InstanceController::hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id) || !txn.get_user(hidden_user_id)) {
      throw ApiError("User does not exist", 400);
    }
    txn.set_hide_user(user_id, hidden_user_id, hidden);
    txn.commit();
  }
  auto InstanceController::hide_board(uint64_t user_id, uint64_t board_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) {
      throw ApiError("User does not exist", 400);
    }
    if (!txn.get_post_stats(board_id)) {
      throw ApiError("Board does not exist", 400);
    }
    txn.set_hide_post(user_id, board_id, hidden);
    txn.commit();
  }
}
