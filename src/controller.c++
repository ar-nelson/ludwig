#include <mutex>
#include <regex>
#include <monocypher.h>
#include <spdlog/spdlog.h>
#include <duthomhas/csprng.hpp>
#include "controller.h++"
#include "webutil.h++"

using std::optional, std::string_view;

namespace Ludwig {
  static constexpr crypto_argon2_config ARGON2_CONFIG = {
    .algorithm = CRYPTO_ARGON2_ID,
    .nb_blocks = 65536,
    .nb_passes = 3,
    .nb_lanes = 1
  };
  //static constexpr uint64_t JWT_DURATION = 86400; // 1 day
  static constexpr double RANK_GRAVITY = 1.8;

  static const std::regex username_regex(R"([\w]{1,64})", std::regex_constants::ECMAScript);
  static const std::regex email_regex(
    R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9]))\.){3}(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))",
    std::regex_constants::ECMAScript | std::regex_constants::icase
  );

  static inline auto rank_numerator(int64_t karma) -> double {
    return std::log(std::max<int64_t>(1, 3 + karma));
  }
  static inline auto rank_denominator(uint64_t time_diff) -> double {
    const uint64_t age_in_hours = time_diff / 3600;
    return std::pow(age_in_hours + 2, RANK_GRAVITY);
  }
  static auto page_rank_cmp(const PageListEntry& a, const PageListEntry& b) -> bool {
    return a.rank > b.rank || (a.rank == b.rank && a.id > b.id);
  }
  static auto note_rank_cmp(const NoteListEntry& a, const NoteListEntry& b) -> bool {
    return a.rank > b.rank || (a.rank == b.rank && a.id > b.id);
  }
  static inline auto next_cursor_page_new(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto page = txn.get_page(*from_id);
    if (!page) return Cursor(prefix, 0, 0);
    return Cursor(prefix, (*page)->created_at(), (*from_id) - 1);
  }
  static inline auto next_cursor_note_new(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto note = txn.get_note(*from_id);
    if (!note) return Cursor(prefix, 0, 0);
    return Cursor(prefix, (*note)->created_at(), (*from_id) - 1);
  }
  static inline auto next_cursor_top(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto stats = txn.get_post_stats(*from_id);
    if (!stats) return Cursor(prefix, 0, 0);
    return Cursor(prefix, karma_uint((*stats)->karma()), (*from_id) - 1);
  }
  static inline auto next_cursor_new_comments(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto stats = txn.get_post_stats(*from_id);
    if (!stats) return Cursor(prefix, 0, 0);
    return Cursor(prefix, (*stats)->latest_comment(), (*from_id) - 1);
  }
  static inline auto next_cursor_most_comments(ReadTxnBase& txn, uint64_t prefix, optional<uint64_t> from_id) -> optional<Cursor> {
    if (!from_id) return {};
    const auto stats = txn.get_post_stats(*from_id);
    if (!stats) return Cursor(prefix, 0, 0);
    return Cursor(prefix, (*stats)->descendant_count(), (*from_id) - 1);
  }
  static inline auto get_page_entry(
    ReadTxnBase& txn,
    uint64_t page_id,
    Controller::Login login,
    optional<const User*> author = {},
    optional<const Board*> board = {}
  ) -> PageListEntry {
    const auto page = txn.get_page(page_id);
    const auto stats = txn.get_post_stats(page_id);
    if (!page || !stats) {
      spdlog::error("Entry references nonexistent post {:x} (database is inconsistent!)", page_id);
      throw ControllerError("Database error", 500);
    }
    if (!author) author = txn.get_user((*page)->author());
    if (!author) {
      spdlog::error(
        "Entry post {:x} references nonexistent author {:x} (database is inconsistent!)",
        page_id, (*page)->author()
      );
      throw ControllerError("Database error", 500);
    }
    if (!board) board = txn.get_board((*page)->board());
    if (!board) {
      spdlog::error(
        "Entry post {:x} references nonexistent board {:x} (database is inconsistent!)",
        page_id, (*page)->board()
      );
      throw ControllerError("Database error", 500);

    }
    const Vote vote = login ? txn.get_vote_of_user_for_post(login->id, page_id) : NoVote;
    return {
      .id = page_id,
      .your_vote = vote,
      .page = *page,
      .stats = *stats,
      .author = *author,
      .board = *board
    };
  }
  static inline auto get_note_entry(
    ReadTxnBase& txn,
    uint64_t note_id,
    Controller::Login login,
    optional<const User*> author = {},
    optional<const Page*> page = {},
    optional<const Board*> board = {}
  ) -> NoteListEntry {
      const auto note = txn.get_note(note_id);
      const auto stats = txn.get_post_stats(note_id);
      if (!note || !stats) {
        spdlog::error("Entry references nonexistent comment {:x} (database is inconsistent!)", note_id);
        throw ControllerError("Database error", 500);
      }
      if (!author) author = txn.get_user((*note)->author());
      if (!author) {
        spdlog::error(
          "Entry comment {:x} references nonexistent author {:x} (database is inconsistent!)",
          note_id, (*note)->author()
        );
        throw ControllerError("Database error", 500);
      }
      if (!page) page = txn.get_page((*note)->page());
      if (!page) {
        spdlog::error(
          "Entry note {:x} references nonexistent post {:x} (database is inconsistent!)",
          note_id, (*note)->page()
        );
        throw ControllerError("Database error", 500);
      }
      if (!board) board = txn.get_board((*page)->board());
      if (!board) {
        spdlog::error(
          "Entry note {:x} references nonexistent board {:x} (database is inconsistent!)",
          note_id, (*page)->board()
        );
        throw ControllerError("Database error", 500);

      }
      const Vote vote = login ? txn.get_vote_of_user_for_post(login->id, note_id) : NoVote;
      return {
        .id = note_id,
        .your_vote = vote,
        .note = *note,
        .stats = *stats,
        .author = *author,
        .page = *page,
        .board = *board
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
    Controller::Login login,
    bool skip_cw,
    std::function<T (uint64_t)> get_entry,
    std::function<uint64_t (T&)> get_timestamp,
    Cmp cmp,
    optional<uint64_t> from = {},
    size_t page_size = ITEMS_PER_PAGE
  ) -> RankedPage<T, Cmp> {
    int64_t max_possible_karma;
    if (iter_by_top.is_done() || iter_by_new.is_done()) return {};
    {
      auto top_stats = txn.get_post_stats(*iter_by_top);
      if (!top_stats) return {};
      max_possible_karma = (*top_stats)->karma();
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
        if (!Controller::should_show(entry, login, skip_cw)) continue;
        const auto timestamp = get_timestamp(entry);
        const auto denominator = rank_denominator(timestamp < now ? now - timestamp : 0);
        entry.rank = rank_numerator(entry.stats->karma()) / denominator;
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
        : std::nullopt
    };
  }

  static auto comment_tree(
    ReadTxnBase& txn,
    CommentTree& tree,
    uint64_t parent,
    CommentSortType sort,
    Controller::Login login,
    bool skip_cw,
    optional<const Page*> page = {},
    optional<const Board*> board = {},
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
        auto ranked = ranked_page<NoteListEntry>(
          txn,
          txn.list_notes_of_post_new(parent),
          txn.list_notes_of_post_top(parent),
          login,
          skip_cw,
          [&](uint64_t id) { return get_note_entry(txn, id, login, {}, page, board); },
          [&](auto& e) { return e.note->created_at(); },
          note_rank_cmp,
          from_id,
          max_comments - tree.size()
        );
        for (auto entry : ranked.page) {
          if (tree.size() >= max_comments) {
            tree.mark_continued(parent, entry.id);
            return;
          }
          const auto id = entry.id, children = entry.stats->child_count();
          tree.emplace(parent, entry);
          if (children) comment_tree(txn, tree, id, sort, login, skip_cw, page, board, {}, max_comments, max_depth - 1);
        }
        if (ranked.next) tree.mark_continued(parent, *ranked.next);
        return;
      }
      case CommentSortType::New:
        iter = txn.list_notes_of_post_new(parent, next_cursor_note_new(txn, parent, from_id));
        break;
      case CommentSortType::Old:
        throw ControllerError("Sort type not yet implemented", 500);
      case CommentSortType::Top:
        iter = txn.list_notes_of_post_top(parent, next_cursor_top(txn, parent, from_id));
        break;
    }
    assert(!!iter);
    for (auto id : *iter) {
      if (tree.size() >= max_comments) {
        tree.mark_continued(parent, id);
        return;
      }
      auto entry = get_note_entry(txn, id, login, {}, page, board);
      if (!Controller::should_show(entry, login, skip_cw)) continue;
      const auto children = entry.stats->child_count();
      tree.emplace(parent, entry);
      if (children) comment_tree(txn, tree, id, sort, login, skip_cw, page, board, {}, max_comments, max_depth - 1);
    }
    if (!iter->is_done()) tree.mark_continued(parent, iter->get_cursor()->int_field_2());
  }

  static inline auto expect_post_stats(ReadTxnBase& txn, uint64_t post_id) -> const PostStats* {
    const auto stats = txn.get_post_stats(post_id);
    if (!stats) {
      spdlog::error("Post {:x} has no corresponding post_stats (database is inconsistent!)", post_id);
      throw ControllerError("Database error");
    }
    return *stats;
  }

  Controller::Controller(std::shared_ptr<DB> db, std::shared_ptr<asio::io_context> io) : db(db), io(io), work(io->get_executor()) {
    auto txn = db->open_read_txn();
    cached_site_detail.domain = std::string(txn.get_setting_str(SettingsKey::domain));
    cached_site_detail.name = std::string(txn.get_setting_str(SettingsKey::name));
    cached_site_detail.description = std::string(txn.get_setting_str(SettingsKey::description));
  }

  auto Controller::hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[32]) -> void {
    // Lock the password hashing step with a mutex,
    // because the Argon2i context is shared
    static std::mutex mutex;
    static uint8_t work_area[ARGON2_CONFIG.nb_blocks * 1024];
    std::lock_guard<std::mutex> lock(mutex);
    spdlog::info("Hashing password {} with salt {}", password.str, string_view(reinterpret_cast<const char*>(salt), 16));
    crypto_argon2(
      hash, 32, work_area, ARGON2_CONFIG, crypto_argon2_inputs {
        .pass = reinterpret_cast<const uint8_t*>(password.str.data()),
        .salt = salt,
        .pass_size = static_cast<uint32_t>(password.str.length()),
        .salt_size = 16
      }, {}
    );
  }

  auto Controller::should_show(const PageListEntry& page, Login login, bool hide_cw) -> bool {
    if (page.page->mod_state() >= ModState::Removed) {
      if (!login || (login->id != page.page->author() && !login->local_user->admin())) return false;
    }
    if (page.page->content_warning() || page.board->content_warning()) {
      if (hide_cw || (login && login->local_user->hide_cw_posts())) return false;
    }
    // TODO: Check if hidden
    return true;
  }
  auto Controller::should_show(const NoteListEntry& note, Login login, bool hide_cw) -> bool {
    if (note.note->mod_state() >= ModState::Removed) {
      if (!login || (login->id != note.note->author() && !login->local_user->admin())) return false;
    }
    if (note.page->mod_state() >= ModState::Removed) {
      if (!login || (login->id != note.page->author() && !login->local_user->admin())) return false;
    }
    if (note.note->content_warning() || note.page->content_warning() || note.board->content_warning()) {
      if (hide_cw || (login && login->local_user->hide_cw_posts())) return false;
    }
    // TODO: Check parent notes
    // TODO: Check if hidden
    return true;
  }
  auto Controller::should_show(const BoardListEntry& board, Login login, bool hide_cw) -> bool {
    if (board.board->content_warning()) {
      if (hide_cw || (login && login->local_user->hide_cw_posts())) return false;
    }
    // TODO: Check if hidden
    return true;
  }
  auto Controller::can_create_page(const BoardListEntry& board, Login login) -> bool {
    if (!login) return false;
    if (board.board->restricted_posting() && !login->local_user->admin()) return false;
    return true;
  }
  auto Controller::can_reply_to(const PageListEntry& page, Login login) -> bool {
    if (!login) return false;
    if (login->local_user->admin()) return true;
    if (page.page->mod_state() >= ModState::Locked) return false;
    return true;
  }
  auto Controller::can_reply_to(const NoteListEntry& note, Login login) -> bool {
    if (!login) return false;
    if (login->local_user->admin()) return true;
    if (note.note->mod_state() >= ModState::Locked || note.page->mod_state() >= ModState::Locked) return false;
    return true;
  }
  auto Controller::can_edit(const PageListEntry& page, Login login) -> bool {
    if (!login || page.page->instance()) return false;
    return login->id == page.page->author() || login->local_user->admin();
  }
  auto Controller::can_edit(const NoteListEntry& note, Login login) -> bool {
    if (!login || note.note->instance()) return false;
    return login->id == note.note->author() || login->local_user->admin();
  }
  auto Controller::can_delete(const PageListEntry& page, Login login) -> bool {
    if (!login || page.page->instance()) return false;
    return login->id == page.page->author() || login->local_user->admin();
  }
  auto Controller::can_delete(const NoteListEntry& note, Login login) -> bool {
    if (!login || note.note->instance()) return false;
    return login->id == note.note->author() || login->local_user->admin();
  }
  auto Controller::can_upvote(const PageListEntry& page, Login login) -> bool {
    if (!login) return false;
    if (!page.board->can_upvote() || page.page->mod_state() >= ModState::Locked) return false;
    return true;
  }
  auto Controller::can_upvote(const NoteListEntry& note, Login login) -> bool {
    if (!login) return false;
    if (!note.board->can_upvote() || note.page->mod_state() >= ModState::Locked || note.note->mod_state() >= ModState::Locked) return false;
    return true;
  }
  auto Controller::can_downvote(const PageListEntry& page, Login login) -> bool {
    if (!login) return false;
    if (!page.board->can_downvote() || page.page->mod_state() >= ModState::Locked) return false;
    return true;
  }
  auto Controller::can_downvote(const NoteListEntry& note, Login login) -> bool {
    if (!login) return false;
    if (!note.board->can_downvote() || note.page->mod_state() >= ModState::Locked || note.note->mod_state() >= ModState::Locked) return false;
    return true;
  }

  auto Controller::login(
    string_view username,
    SecretString&& password,
    string_view ip,
    string_view user_agent
  ) -> LoginResponse {
    uint8_t hash[32];
    auto txn = db->open_write_txn();
    const auto user_id_opt = txn.get_user_id(username);
    if (!user_id_opt) {
      spdlog::debug("Tried to log in as nonexistent user {}", username);
      throw ControllerError("Invalid username or password", 400);
    }
    const auto user_id = *user_id_opt;
    const auto local_user = txn.get_local_user(user_id);
    if (!local_user) {
      spdlog::debug("Tried to log in as non-local user {}", username);
      throw ControllerError("Invalid username or password", 400);
    }
    hash_password(std::move(password), (*local_user)->password_salt()->bytes()->Data(), hash);

    // Note that this returns 0 on success, 1 on failure!
    if (crypto_verify32(hash, (*local_user)->password_hash()->bytes()->Data())) {
      // TODO: Lock users out after repeated failures
      spdlog::debug("Tried to login with wrong password for user {}", username);
      throw ControllerError("Invalid username or password", 400);
    }
    const auto session = txn.create_session(user_id, ip, user_agent);
    txn.commit();
    return { .user_id = user_id, .session_id = session.first, .expiration = session.second };
  }
  auto Controller::page_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    bool skip_cw,
    std::optional<uint64_t> from_id
  ) -> PageDetailResponse {
    PageDetailResponse rsp { get_page_entry(txn, id, login), {} };
    comment_tree(txn, rsp.comments, id, sort, login, skip_cw, rsp.page, rsp.board, from_id);
    return rsp;
  }
  auto Controller::note_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    bool skip_cw,
    std::optional<uint64_t> from_id
  ) -> NoteDetailResponse {
    NoteDetailResponse rsp { get_note_entry(txn, id, login), {} };
    comment_tree(txn, rsp.comments, id, sort, login, skip_cw, rsp.page, rsp.board, from_id);
    return rsp;
  }
  auto Controller::user_detail(ReadTxnBase& txn, uint64_t id) -> UserDetailResponse {
    auto user = txn.get_user(id);
    auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ControllerError("User not found", 404);
    return { { id, *user, }, *user_stats };
  }
  auto Controller::local_user_detail(ReadTxnBase& txn, uint64_t id) -> LocalUserDetailResponse {
    const auto local_user = txn.get_local_user(id);
    if (!local_user) throw ControllerError("Local user not found", 404);
    const auto user = txn.get_user(id);
    const auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ControllerError("User not found", 404);
    return { { { id, *user }, *user_stats, }, *local_user };
  }
  auto Controller::board_detail(ReadTxnBase& txn, uint64_t id) -> BoardDetailResponse {
    const auto board = txn.get_board(id);
    const auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ControllerError("Board not found", 404);
    return { { id, *board }, *board_stats };
  }
  auto Controller::list_local_users(ReadTxnBase& txn, optional<uint64_t> from_id) -> ListUsersResponse {
    ListUsersResponse out { {}, 0, !from_id, {} };
    auto iter = txn.list_local_users(from_id ? optional(Cursor(*from_id)) : std::nullopt);
    for (auto id : iter) {
      const auto user = txn.get_user(id);
      if (!user) {
        spdlog::warn("Local user {:x} has no corresponding user entry (database is inconsistent!)", id);
        continue;
      }
      out.page[out.size] = { id, *user };
      if (++out.size >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  auto Controller::list_local_boards(ReadTxnBase& txn, optional<uint64_t> from_id) -> ListBoardsResponse {
    ListBoardsResponse out { {}, 0, !from_id, {} };
    auto iter = txn.list_local_boards(from_id ? optional(Cursor(*from_id)) : std::nullopt);
    for (auto id : iter) {
      const auto board = txn.get_board(id);
      if (!board) {
        spdlog::warn("Local board {:x} has no corresponding board entry (database is inconsistent!)", id);
        continue;
      }
      out.page[out.size] = { id, *board };
      if (++out.size >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  auto Controller::list_board_pages(
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> ListPagesResponse {
    ListPagesResponse out { {}, 0, !from_id, {} };
    const auto board = txn.get_board(board_id);
    if (!board) {
      throw ControllerError("Board does not exist", 404);
    }
    switch (sort) {
      case SortType::New: {
        auto iter = txn.list_pages_of_board_new(board_id, next_cursor_page_new(txn, board_id, from_id));
        for (uint64_t page_id : iter) {
          out.page[out.size] = get_page_entry(txn, page_id, login, {}, board);
          if (skip_cw && out.page[out.size].page->content_warning()) continue;
          if (++out.size >= ITEMS_PER_PAGE) break;
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
        auto iter = txn.list_pages_of_board_top(board_id, next_cursor_top(txn, board_id, from_id));
        uint64_t earliest;
        switch (sort) {
          case SortType::TopYear:
            earliest = now_s() - 86400 * 365;
            break;
          case SortType::TopSixMonths:
            earliest = now_s() - 86400 * 30 * 6;
            break;
          case SortType::TopThreeMonths:
            earliest = now_s() - 86400 * 30 * 3;
            break;
          case SortType::TopMonth:
            earliest = now_s() - 86400 * 30;
            break;
          case SortType::TopWeek:
            earliest = now_s() - 86400 * 7;
            break;
          case SortType::TopDay:
            earliest = now_s() - 86400;
            break;
          case SortType::TopTwelveHour:
            earliest = now_s() - 3600 * 12;
            break;
          case SortType::TopSixHour:
            earliest = now_s() - 3600 * 6;
            break;
          case SortType::TopHour:
            earliest = now_s() - 3600;
            break;
          default:
            earliest = 0;
        }
        for (uint64_t page_id : iter) {
          const auto page = txn.get_page(page_id);
          if (page && ((*page)->created_at() < earliest || (skip_cw && (*page)->content_warning()))) {
            continue;
          }
          out.page[out.size] = get_page_entry(txn, page_id, login, {}, board);
          if (++out.size >= ITEMS_PER_PAGE) break;
        }
        if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_2() };
        break;
      }
      case SortType::Hot: {
        auto ranked = ranked_page<PageListEntry>(
          txn,
          txn.list_pages_of_board_new(board_id),
          txn.list_pages_of_board_top(board_id),
          login,
          skip_cw,
          [&](uint64_t id) { return get_page_entry(txn, id, login, {}, board); },
          [&](auto& e) { return e.page->created_at(); },
          page_rank_cmp,
          from_id
        );
        for (auto entry : ranked.page) {
          out.page[out.size] = entry;
          if (++out.size >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        break;
      }
      default:
        throw ControllerError("Sort type not yet supported");
    }
    return out;
  }
  auto Controller::list_board_notes(
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> ListNotesResponse {
    ListNotesResponse out { {}, 0, !from_id, {} };
    const auto board = txn.get_board(board_id);
    if (!board) {
      throw ControllerError("Board does not exist", 404);
    }
    switch (sort) {
      case SortType::New: {
        auto iter = txn.list_notes_of_board_new(board_id, next_cursor_note_new(txn, board_id, from_id));
        for (uint64_t note_id : iter) {
          out.page[out.size] = get_note_entry(txn, note_id, login, {}, {}, board);
          if (skip_cw && out.page[out.size].page->content_warning()) continue;
          if (++out.size >= ITEMS_PER_PAGE) break;
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
        auto iter = txn.list_notes_of_board_top(board_id, next_cursor_top(txn, board_id, from_id));
        uint64_t earliest;
        switch (sort) {
          case SortType::TopYear:
            earliest = now_s() - 86400 * 365;
            break;
          case SortType::TopSixMonths:
            earliest = now_s() - 86400 * 30 * 6;
            break;
          case SortType::TopThreeMonths:
            earliest = now_s() - 86400 * 30 * 3;
            break;
          case SortType::TopMonth:
            earliest = now_s() - 86400 * 30;
            break;
          case SortType::TopWeek:
            earliest = now_s() - 86400 * 7;
            break;
          case SortType::TopDay:
            earliest = now_s() - 86400;
            break;
          case SortType::TopTwelveHour:
            earliest = now_s() - 3600 * 12;
            break;
          case SortType::TopSixHour:
            earliest = now_s() - 3600 * 6;
            break;
          case SortType::TopHour:
            earliest = now_s() - 3600;
            break;
          default:
            earliest = 0;
        }
        for (uint64_t note_id : iter) {
          const auto note = txn.get_note(note_id);
          if (note && (*note)->created_at() < earliest) continue;
          out.page[out.size] = get_note_entry(txn, note_id, login, {}, {}, board);
          if (!should_show(out.page[out.size], login, skip_cw)) continue;
          if (++out.size >= ITEMS_PER_PAGE) break;
        }
        if (!iter.is_done()) out.next = { iter.get_cursor()->int_field_2() };
        break;
      }
      case SortType::Hot: {
        auto ranked = ranked_page<NoteListEntry>(
          txn,
          txn.list_notes_of_board_new(board_id),
          txn.list_notes_of_board_top(board_id),
          login,
          skip_cw,
          [&](uint64_t id) { return get_note_entry(txn, id, login, {}, {}, board); },
          [&](auto& e) { return e.note->created_at(); },
          note_rank_cmp,
          from_id
        );
        for (auto entry : ranked.page) {
          out.page[out.size] = entry;
          if (++out.size >= ITEMS_PER_PAGE) break;
        }
        out.next = ranked.next;
        break;
      }
      default:
        throw ControllerError("Sort type not yet supported");
    }
    return out;
  }
  auto Controller::list_user_pages(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> ListPagesResponse {
    ListPagesResponse out { {}, 0, !from_id, {} };
    const auto user = txn.get_user(user_id);
    if (!user) {
      throw ControllerError("User does not exist", 404);
    }
    // TODO: Old sort
    auto iter = sort == UserPostSortType::Top
      ? txn.list_pages_of_user_top(user_id, next_cursor_top(txn, user_id, from_id))
      : txn.list_pages_of_user_new(user_id, from_id ? optional(Cursor(user_id, *from_id)) : std::nullopt);
    for (uint64_t page_id : iter) {
      out.page[out.size] = get_page_entry(txn, page_id, login, user);
      if (!should_show(out.page[out.size], login, skip_cw)) continue;
      if (++out.size >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = {
      sort == UserPostSortType::Top ? iter.get_cursor()->int_field_2() : iter.get_cursor()->int_field_1()
    };
    return out;
  }
  auto Controller::list_user_notes(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    bool skip_cw,
    optional<uint64_t> from_id
  ) -> ListNotesResponse {
    ListNotesResponse out { {}, 0, !from_id, {} };
    // TODO: Old sort
    auto iter = sort == UserPostSortType::Top
      ? txn.list_notes_of_user_top(user_id, next_cursor_top(txn, user_id, from_id))
      : txn.list_notes_of_user_new(user_id, from_id ? optional(Cursor(user_id, *from_id)) : std::nullopt);
    for (uint64_t note_id : iter) {
      out.page[out.size] = get_note_entry(txn, note_id, login);
      if (!should_show(out.page[out.size], login, skip_cw)) continue;
      if (++out.size >= ITEMS_PER_PAGE) break;
    }
    if (!iter.is_done()) out.next = {
      sort == UserPostSortType::Top ? iter.get_cursor()->int_field_2() : iter.get_cursor()->int_field_1()
    };
    return out;
  }
  auto Controller::create_local_user(
    string_view username,
    string_view email,
    SecretString&& password
  ) -> uint64_t {
    if (!std::regex_match(username.begin(), username.end(), username_regex)) {
      throw ControllerError("Invalid username (only letters, numbers, and underscores allowed; max 64 characters)", 400);
    }
    if (!std::regex_match(email.begin(), email.end(), email_regex)) {
      throw ControllerError("Invalid email address", 400);
    }
    if (password.str.length() < 8) {
      throw ControllerError("Password must be at least 8 characters", 400);
    }
    auto txn = db->open_write_txn();
    if (txn.get_user_id(username)) {
      throw ControllerError("A user with this name already exists on this instance", 409);
    }
    // TODO: Check if email already exists
    uint8_t salt[16], hash[32];
    duthomhas::csprng rng;
    rng(salt);
    hash_password(std::move(password), salt, hash);
    flatbuffers::FlatBufferBuilder fbb;
    {
      UserBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_name(fbb.CreateString(username));
      fbb.Finish(b.Finish());
    }
    const auto user_id = txn.create_user(fbb);
    fbb.Clear();
    {
      LocalUserBuilder b(fbb);
      b.add_email(fbb.CreateString(email));
      Hash hash_struct(hash);
      b.add_password_hash(&hash_struct);
      Salt salt_struct(salt);
      b.add_password_salt(&salt_struct);
      fbb.Finish(b.Finish());
    }
    txn.set_local_user(user_id, fbb);
    txn.commit();
    return user_id;
  }
  auto Controller::create_local_board(
    uint64_t owner,
    string_view name,
    optional<string_view> display_name,
    optional<string_view> content_warning,
    bool is_private,
    bool is_restricted_posting,
    bool is_local_only
  ) -> uint64_t {
    if (!std::regex_match(name.begin(), name.end(), username_regex)) {
      throw ControllerError("Invalid board name (only letters, numbers, and underscores allowed; max 64 characters)", 400);
    }
    if (display_name && display_name->length() > 1024) {
      throw ControllerError("Display name cannot be longer than 1024 bytes", 400);
    }
    auto txn = db->open_write_txn();
    if (txn.get_board_id(name)) {
      throw ControllerError("A board with this name already exists on this instance", 409);
    }
    if (!txn.get_local_user(owner)) {
      throw ControllerError("Board owner is not a user on this instance", 400);
    }
    // TODO: Check if user is allowed to create boards
    flatbuffers::FlatBufferBuilder fbb;
    {
      BoardBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_name(fbb.CreateString(name));
      if (display_name) b.add_display_name(fbb.CreateString(*display_name));
      if (content_warning) b.add_content_warning(fbb.CreateString(*content_warning));
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
  auto Controller::create_local_page(
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
    uint64_t page_id;
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
      PageBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_author(author);
      b.add_board(board);
      b.add_title(fbb.CreateString(title));
      if (submission_url) b.add_content_url(fbb.CreateString(*submission_url));
      if (text_content_markdown) {
        // TODO: Parse Markdown and HTML
        b.add_content_text_raw(fbb.CreateString(*text_content_markdown));
        b.add_content_text_safe(fbb.CreateString(escape_html(*text_content_markdown)));
      }
      if (content_warning) b.add_content_warning(fbb.CreateString(*content_warning));
      fbb.Finish(b.Finish());
      page_id = txn.create_page(fbb);
      txn.set_vote(author, page_id, Upvote);
      txn.commit();
    }
    dispatch_event(Event::UserStatsUpdate, author);
    dispatch_event(Event::BoardStatsUpdate, board);
    return page_id;
  }
  auto Controller::create_local_note(
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
    uint64_t note_id, page_id, board_id;
    {
      auto txn = db->open_write_txn();
      if (!txn.get_local_user(author)) {
        throw ControllerError("Comment author is not a user on this instance", 400);
      }
      auto parent_page = txn.get_page(parent);
      const auto parent_note = !parent_page ? txn.get_note(parent) : std::nullopt;
      if (parent_note) parent_page = txn.get_page((*parent_note)->page());
      if (!parent_page) {
        throw ControllerError("Comment parent post does not exist", 400);
      }
      board_id = (*parent_page)->board();
      // TODO: Check if user is banned
      flatbuffers::FlatBufferBuilder fbb;
      NoteBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_author(author);
      b.add_page(page_id = parent_note ? (*parent_note)->page() : parent);
      // TODO: Parse Markdown and HTML
      b.add_content_raw(fbb.CreateString(text_content_markdown));
      b.add_content_safe(fbb.CreateString(escape_html(text_content_markdown)));
      if (content_warning) b.add_content_warning(fbb.CreateString(*content_warning));
      fbb.Finish(b.Finish());
      note_id = txn.create_note(fbb);
      txn.set_vote(author, note_id, Upvote);
      txn.commit();
    }
    dispatch_event(Event::UserStatsUpdate, author);
    dispatch_event(Event::BoardStatsUpdate, board_id);
    dispatch_event(Event::PageStatsUpdate, page_id);
    if (parent != page_id) dispatch_event(Event::NoteStatsUpdate, parent);
    return note_id;
  }
  auto Controller::vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_user(user_id)) {
      throw ControllerError("User does not exist", 400);
    }
    const auto page = txn.get_page(post_id);
    const auto note = !page ? txn.get_note(post_id) : std::nullopt;
    if (!page && !note) {
      throw ControllerError("Post does not exist", 400);
    }
    const auto op = page ? (*page)->author() : (*note)->author();
    txn.set_vote(user_id, post_id, vote);
    txn.commit();

    dispatch_event(Event::UserStatsUpdate, op);
    if (page) dispatch_event(Event::PageStatsUpdate, post_id);
    if (note) dispatch_event(Event::NoteStatsUpdate, post_id);
  }
  auto Controller::subscribe(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
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

  auto Controller::dispatch_event(Event event, uint64_t subject_id) -> void {
    std::shared_lock<std::shared_mutex> lock(listener_lock);
    if (event == Event::SiteUpdate) subject_id = 0;
    auto range = event_listeners.equal_range({ event, subject_id });
    for (auto i = range.first; i != range.second; i++) {
      io->dispatch((*i).second);
    }
  }

  auto Controller::on_event(Event event, uint64_t subject_id, EventCallback&& callback) -> EventSubscription {
    std::unique_lock<std::shared_mutex> lock(listener_lock);
    auto id = next_event_id++;
    event_listeners.emplace(std::pair(event, subject_id), EventListener(id, event, subject_id, std::move(callback)));
    return EventSubscription(shared_from_this(), id, event, subject_id);
  }

  EventSubscription::~EventSubscription() {
    if (auto ctrl = controller.lock()) {
      std::unique_lock<std::shared_mutex> lock(ctrl->listener_lock);
      auto range = ctrl->event_listeners.equal_range(key);
      ctrl->event_listeners.erase(std::find_if(range.first, range.second, [this](auto p) {
        return p.second.id == this->id;
      }));
    }
  }
}
