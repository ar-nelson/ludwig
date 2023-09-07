#include <mutex>
#include <monocypher.h>
#include <spdlog/spdlog.h>
#include "controller.h++"
#include "jwt.h++"

namespace Ludwig {
  static constexpr crypto_argon2_config ARGON2_CONFIG = {
    .algorithm = CRYPTO_ARGON2_I,
    .nb_blocks = 100'000,
    .nb_passes = 3,
    .nb_lanes = 1
  };
  static constexpr uint64_t JWT_DURATION = 86400; // 1 day
  static constexpr double RANK_GRAVITY = 1.8;

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
  static auto page_of_ranked_pages(
    ReadTxn& txn,
    DBIter<uint64_t> iter_by_new,
    int64_t max_possible_karma,
    bool active
  ) -> std::set<PageListEntry, decltype(page_rank_cmp)*> {
    const auto max_possible_numerator = rank_numerator(max_possible_karma);
    const auto now = now_s();
    std::set<PageListEntry, decltype(page_rank_cmp)*> sorted_pages(page_rank_cmp);
    for (auto page_id : iter_by_new) {
      const auto page = txn.get_page(page_id);
      const auto stats = txn.get_page_stats(page_id);
      if (!page || !stats) {
        spdlog::warn("Board entry references nonexistent post {:x} (database is inconsistent!)", page_id);
        continue;
      }
      const auto timestamp = active ? (*stats)->newest_comment_time() : (*page)->created_at();
      const auto denominator = rank_denominator(timestamp < now ? now - timestamp : 0);
      if (sorted_pages.size() >= PAGE_SIZE) {
        const double max_possible_rank = max_possible_numerator / denominator;
        auto last = std::prev(sorted_pages.end());
        if (max_possible_rank <= last->rank) break;
        sorted_pages.erase(last);
      }
      const double rank = rank_numerator((*stats)->karma()) / denominator;
      sorted_pages.emplace(PageListEntry{page_id, rank, *page, *stats});
    }
    return sorted_pages;
  }
  static auto page_of_ranked_notes(
    ReadTxn& txn,
    DBIter<uint64_t> iter_by_new,
    int64_t max_possible_karma
  ) -> std::set<NoteListEntry, decltype(note_rank_cmp)*> {
    const auto max_possible_numerator = rank_numerator(max_possible_karma);
    const auto now = now_s();
    std::set<NoteListEntry, decltype(note_rank_cmp)*> sorted_notes(note_rank_cmp);
    for (auto note_id : iter_by_new) {
      const auto note = txn.get_note(note_id);
      const auto stats = txn.get_note_stats(note_id);
      if (!note || !stats) {
        spdlog::warn("Board entry references nonexistent comment {:x} (database is inconsistent!)", note_id);
        continue;
      }
      const auto timestamp = (*note)->created_at();
      const auto denominator = rank_denominator(timestamp < now ? now - timestamp : 0);
      if (sorted_notes.size() >= PAGE_SIZE) {
        const double max_possible_rank = max_possible_numerator / denominator;
        auto last = std::prev(sorted_notes.end());
        if (max_possible_rank <= last->rank) break;
        sorted_notes.erase(last);
      }
      const double rank = rank_numerator((*stats)->karma()) / denominator;
      sorted_notes.emplace(NoteListEntry{note_id, rank, *note, *stats});
    }
    return sorted_notes;
  }

  static inline auto expect_page_stats(ReadTxn& txn, uint64_t page_id) -> const PageStats* {
    const auto stats = txn.get_page_stats(page_id);
    if (!stats) {
      spdlog::error("Post {:x} has no corresponding page_stats (database is inconsistent!)", page_id);
      throw ControllerError("Database error");
    }
    return *stats;
  }

  Controller::Controller(DB& db) : db(db) {
    auto txn = db.open_read_txn();
    cached_site_detail.domain = std::string(txn.get_setting_str(SettingsKey::domain));
    cached_site_detail.name = std::string(txn.get_setting_str(SettingsKey::name));
    cached_site_detail.description = std::string(txn.get_setting_str(SettingsKey::description));
  }

  auto Controller::get_auth_user(SecretString&& jwt) -> uint64_t {
    auto payload = parse_jwt(jwt.str, db.jwt_secret);
    if (!payload) throw ControllerError("Session expired or not authorized", 401);
    return payload->sub;
  }
  auto Controller::hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[64]) -> void {
    // Lock the password hashing step with a mutex,
    // because the Argon2i context is shared
    static std::mutex mutex;
    static uint8_t work_area[ARGON2_CONFIG.nb_blocks * 1024];
    std::lock_guard<std::mutex> lock(mutex);
    crypto_argon2(
      hash, 64, work_area, ARGON2_CONFIG, crypto_argon2_inputs {
        .pass = reinterpret_cast<uint8_t*>(password.str.data()),
        .salt = salt,
        .pass_size = static_cast<uint32_t>(password.str.length()),
        .salt_size = 16
      }, {}
    );
  }
  auto Controller::login(std::string_view username, SecretString&& password) -> LoginResponse {
    uint64_t user_id;
    uint8_t salt[16], hash[64], actual_hash[64];
    {
      auto txn = db.open_read_txn();
      auto user_id_opt = txn.get_user_id(username);
      if (!user_id_opt) {
        spdlog::debug("Tried to log in as nonexistent user {}", username);
        throw ControllerError("Invalid username or password", 400);
      }
      user_id = *user_id_opt;
      auto local_user = txn.get_local_user(user_id);
      if (!local_user) {
        spdlog::debug("Tried to log in as non-local user {}", username);
        throw ControllerError("Invalid username or password", 400);
      }
      memcpy(salt, (*local_user)->password_salt()->bytes()->Data(), 16);
      memcpy(actual_hash, (*local_user)->password_hash()->bytes()->Data(), 64);
    }
    hash_password(std::move(password), salt, actual_hash);
    if (!crypto_verify64(hash, actual_hash)) {
      // TODO: Lock users out after repeated failures
      spdlog::debug("Tried to login with wrong password for user {}", username);
      throw ControllerError("Invalid username or password", 400);
    }
    auto jwt = make_jwt(user_id, JWT_DURATION, db.jwt_secret);
    return { .user_id = user_id, .jwt = { std::move(jwt) } };
  }
  auto Controller::page_detail(uint64_t id) -> PageDetailResponse {
    auto txn = db.open_read_txn();
    auto page = txn.get_page(id);
    auto page_stats = txn.get_page_stats(id);
    if (!page || !page_stats) throw ControllerError("Post not found", 404);
    return { std::move(txn), *page, *page_stats };
  }
  auto Controller::user_detail(uint64_t id) -> UserDetailResponse {
    auto txn = db.open_read_txn();
    auto user = txn.get_user(id);
    auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ControllerError("User not found", 404);
    return { std::move(txn), *user, *user_stats };
  }
  auto Controller::board_detail(uint64_t id) -> BoardDetailResponse {
    auto txn = db.open_read_txn();
    auto board = txn.get_board(id);
    auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ControllerError("Board not found", 404);
    return { std::move(txn), *board, *board_stats };
  }
  auto Controller::list_local_users(uint64_t from_id) -> ListUsersResponse {
    ListUsersResponse out { db.open_read_txn(), {}, 0, {} };
    auto iter = out.txn.list_local_users(from_id ? std::optional(Cursor(from_id)) : std::nullopt);
    for (auto id : iter) {
      auto user = out.txn.get_user(id);
      if (!user) {
        spdlog::warn("Local user {:x} has no corresponding user entry (database is inconsistent!)", id);
        continue;
      }
      out.page[out.size] = { id, *user };
      if (++out.size >= PAGE_SIZE) break;
    }
    if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  auto Controller::list_local_boards(uint64_t from_id) -> ListBoardsResponse {
    ListBoardsResponse out { db.open_read_txn(), {}, 0, {} };
    auto iter = out.txn.list_local_boards(from_id ? std::optional(Cursor(from_id)) : std::nullopt);
    for (auto id : iter) {
      auto board = out.txn.get_board(id);
      if (!board) {
        spdlog::warn("Local board {:x} has no corresponding board entry (database is inconsistent!)", id);
        continue;
      }
      out.page[out.size] = { id, *board };
      if (++out.size >= PAGE_SIZE) break;
    }
    if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  auto Controller::list_board_pages(uint64_t board_id, SortType sort, uint64_t from_id) -> ListPagesResponse {
    ListPagesResponse out { db.open_read_txn(), {}, 0, {} };
    switch (sort) {
      case SortType::New: {
        auto iter = out.txn.list_pages_of_board_new(board_id, {Cursor(from_id)});
        for (uint64_t page_id : iter) {
          const auto page = out.txn.get_page(page_id);
          const auto stats = out.txn.get_page_stats(page_id);
          if (!page || !stats) {
            spdlog::warn("Board entry references nonexistent post {:x} (database is inconsistent!)", page_id);
            continue;
          }
          out.page[out.size] = { page_id, 0, *page, *stats };
          if (++out.size >= PAGE_SIZE) break;
        }
        if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
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
        auto iter = out.txn.list_pages_of_board_top(board_id, {Cursor(from_id)});
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
          const auto page = out.txn.get_page(page_id);
          const auto stats = out.txn.get_page_stats(page_id);
          if (!page || !stats) {
            spdlog::warn("Board entry references nonexistent post {:x} (database is inconsistent!)", page_id);
            continue;
          }
          if ((*page)->created_at() < earliest) continue;
          out.page[out.size] = { page_id, 0, *page, *stats };
          if (++out.size >= PAGE_SIZE) break;
        }
        if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
        break;
      }
      case SortType::Hot: {
        int64_t highest_karma;
        {
          auto iter = out.txn.list_pages_of_board_top(board_id);
          if (iter.is_done()) break;
          highest_karma = expect_page_stats(out.txn, *iter)->karma();
        }
        for (auto entry : page_of_ranked_pages(out.txn, out.txn.list_pages_of_board_new(board_id, {Cursor(from_id)}), highest_karma, false)) {
          out.page[out.size] = entry;
          if (++out.size >= PAGE_SIZE) break;
        }
        if (out.size >= PAGE_SIZE) {
          for (auto page_id : out.txn.list_pages_of_board_new(board_id, {Cursor(from_id)})) {
            if (std::none_of(out.page.begin(), out.page.end(), [page_id](const PageListEntry& e) { return e.id == page_id; })) {
              out.next = { page_id };
              break;
            }
          }
        }
        break;
      }
      default:
        throw ControllerError("Sort type not yet supported");
    }
    return out;
  }
  auto Controller::list_board_notes(uint64_t board_id, CommentSortType sort, uint64_t from_id) -> ListNotesResponse {
    ListNotesResponse out { db.open_read_txn(), {}, 0, {} };
    // TODO: Old sort
    auto iter = sort == CommentSortType::Top
      ? out.txn.list_notes_of_board_top(board_id, {Cursor(from_id)})
      : out.txn.list_notes_of_board_new(board_id, {Cursor(from_id)});
    if (sort == CommentSortType::Hot) {
      int64_t highest_karma;
      {
        auto iter = out.txn.list_pages_of_board_top(board_id);
        if (iter.is_done()) return out;
        highest_karma = expect_page_stats(out.txn, *iter)->karma();
      }
      for (auto entry : page_of_ranked_notes(out.txn, std::move(iter), highest_karma)) {
        out.page[out.size] = entry;
        if (++out.size >= PAGE_SIZE) break;
      }
      if (out.size >= PAGE_SIZE) {
        for (auto note_id : out.txn.list_notes_of_board_new(board_id, {Cursor(from_id)})) {
          if (std::none_of(out.page.begin(), out.page.end(), [note_id](const NoteListEntry& e) { return e.id == note_id; })) {
            out.next = { note_id };
            break;
          }
        }
      }
    } else {
      for (uint64_t note_id : iter) {
        const auto note = out.txn.get_note(note_id);
        const auto stats = out.txn.get_note_stats(note_id);
        if (!note || !stats) {
          spdlog::warn("Board entry references nonexistent comment {:x} (database is inconsistent!)", note_id);
          continue;
        }
        out.page[out.size] = { note_id, 0, *note, *stats };
        if (++out.size >= PAGE_SIZE) break;
      }
      if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
    }
    return out;
  }
  auto Controller::list_child_notes(uint64_t parent_id, CommentSortType sort, uint64_t from_id) -> ListNotesResponse {
    ListNotesResponse out { db.open_read_txn(), {}, 0, {} };
    // TODO: Old sort
    auto iter = sort == CommentSortType::Top
      ? out.txn.list_notes_of_post_top(parent_id, {Cursor(from_id)})
      : out.txn.list_notes_of_post_new(parent_id, {Cursor(from_id)});
    if (sort == CommentSortType::Hot) {
      int64_t highest_karma;
      {
        auto iter = out.txn.list_notes_of_post_top(parent_id);
        if (iter.is_done()) return out;
        highest_karma = expect_page_stats(out.txn, *iter)->karma();
      }
      for (auto entry : page_of_ranked_notes(out.txn, std::move(iter), highest_karma)) {
        out.page[out.size] = entry;
        if (++out.size >= PAGE_SIZE) break;
      }
      if (out.size >= PAGE_SIZE) {
        for (auto note_id : out.txn.list_notes_of_post_new(parent_id, {Cursor(from_id)})) {
          if (std::none_of(out.page.begin(), out.page.end(), [note_id](const NoteListEntry& e) { return e.id == note_id; })) {
            out.next = { note_id };
            break;
          }
        }
      }
    } else {
      for (uint64_t note_id : iter) {
        const auto note = out.txn.get_note(note_id);
        const auto stats = out.txn.get_note_stats(note_id);
        if (!note || !stats) {
          spdlog::warn("Comment list entry references nonexistent comment {:x} (database is inconsistent!)", note_id);
          continue;
        }
        out.page[out.size] = { note_id, 0, *note, *stats };
        if (++out.size >= PAGE_SIZE) break;
      }
      if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
    }
    return out;
  }
  auto Controller::list_user_pages(uint64_t user_id, UserPostSortType sort, uint64_t from_id) -> ListPagesResponse {
    ListPagesResponse out { db.open_read_txn(), {}, 0, {} };
    // TODO: Old sort
    auto iter = sort == UserPostSortType::Top
      ? out.txn.list_pages_of_user_top(user_id, {Cursor(from_id)})
      : out.txn.list_pages_of_user_new(user_id, {Cursor(from_id)});
    for (uint64_t page_id : iter) {
      const auto page = out.txn.get_page(page_id);
      const auto stats = out.txn.get_page_stats(page_id);
      if (!page || !stats) {
        spdlog::warn("User entry references nonexistent post {:x} (database is inconsistent!)", page_id);
        continue;
      }
      out.page[out.size] = { page_id, 0, *page, *stats };
      if (++out.size >= PAGE_SIZE) break;
    }
    if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  auto Controller::list_user_notes(uint64_t user_id, UserPostSortType sort, uint64_t from_id) -> ListNotesResponse {
    ListNotesResponse out { db.open_read_txn(), {}, 0, {} };
    // TODO: Old sort
    auto iter = sort == UserPostSortType::Top
      ? out.txn.list_notes_of_user_top(user_id, {Cursor(from_id)})
      : out.txn.list_notes_of_user_new(user_id, {Cursor(from_id)});
    for (uint64_t note_id : iter) {
      const auto note = out.txn.get_note(note_id);
      const auto stats = out.txn.get_note_stats(note_id);
      if (!note || !stats) {
        spdlog::warn("User entry references nonexistent comment {:x} (database is inconsistent!)", note_id);
        continue;
      }
      out.page[out.size] = { note_id, 0, *note, *stats };
      if (++out.size >= PAGE_SIZE) break;
    }
    if (!iter.done) out.next = { iter.get_cursor()->int_field_0() };
    return out;
  }
  auto Controller::create_local_user() -> uint64_t {

  }
  auto Controller::create_local_board() -> uint64_t {

  }
  auto Controller::create_local_page() -> uint64_t {

  }
  auto Controller::create_local_note() -> uint64_t {

  }
  auto Controller::vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {

  }
  auto Controller::subscribe(uint64_t user_id, uint64_t board_id) -> void {

  }
  auto Controller::unsubscribe(uint64_t user_id, uint64_t board_id) -> void {

  }
}
