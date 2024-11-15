#include "post_controller.h++"
#include "util/rich_text.h++"
#include "models/board.h++"
#include <queue>

using std::function, std::generator, std::min, std::max, std::nullopt, std::optional, std::pair, std::priority_queue,
  std::string_view, std::vector, flatbuffers::FlatBufferBuilder, fmt::format,
  fmt::operator""_cf; // NOLINT
namespace chrono = std::chrono;

namespace Ludwig {

static constexpr double RANK_GRAVITY = 1.8;

static inline auto rank_numerator(int64_t karma) -> double {
  return std::log(max<int64_t>(1, 3 + karma));
}
static inline auto rank_denominator(chrono::duration<double> time_diff) -> double {
  return std::pow(max(0L, chrono::duration_cast<chrono::hours>(time_diff).count()) + 2L, RANK_GRAVITY);
}
template <typename T> static auto latest_comment_cmp(const T& a, const T& b) -> bool {
  return a.stats().latest_comment() > b.stats().latest_comment();
}
using RankedId = pair<uint64_t, double>;
static auto ranked_id_cmp(const RankedId& a, const RankedId& b) -> bool {
  return a.second < b.second;
}

using RankedQueue = priority_queue<RankedId, vector<RankedId>, decltype(ranked_id_cmp)*>;

enum class RankType : bool { Active, Hot };

template <RankType Type, class T, class Fn>
static inline auto ranked(
  ReadTxn& txn,
  PageCursor& cursor,
  DBIter iter_by_new,
  DBIter iter_by_top,
  Fn get_entry,
  double max_rank = INFINITY
) -> generator<const T&> {
  int64_t max_possible_karma;
  if (iter_by_top.is_done() || iter_by_new.is_done()) {
    cursor.reset();
    co_return;
  }
  if (auto top_stats = txn.get_post_stats(*iter_by_top)) {
    max_possible_karma = top_stats->get().karma();
  } else {
    cursor.reset();
    co_return;
  }
  const auto max_possible_numerator = rank_numerator(max_possible_karma);
  const auto now = now_t();
  RankedQueue queue(ranked_id_cmp);
  for (const uint64_t id : iter_by_new) {
    const auto stats = txn.get_post_stats(id);
    if (!stats) continue;
    Timestamp timestamp;
    if constexpr (Type == RankType::Active) {
      timestamp = uint_to_timestamp(stats->get().latest_comment());
    } else {
      timestamp = T::get_created_at(txn, id);
    }
    const double denominator = rank_denominator(now - timestamp),
      rank = rank_numerator(stats->get().karma()) / denominator;
    if (rank >= max_rank) continue;
    queue.emplace(id, rank);
    const auto [top_id, top_rank] = queue.top();
    double max_possible_rank;
    if constexpr (Type == RankType::Active) {
      const auto latest_possible_timestamp = min(now, T::get_created_at(txn, id) + ACTIVE_COMMENT_MAX_AGE);
      const double min_possible_denominator =
        rank_denominator(latest_possible_timestamp < now ? now - latest_possible_timestamp : chrono::seconds::zero());
      max_possible_rank = max_possible_numerator / min_possible_denominator;
    } else {
      max_possible_rank = max_possible_numerator / denominator;
    }
    if (max_possible_rank > top_rank) continue;
    cursor.set(*reinterpret_cast<const uint64_t*>(&top_rank), top_id);
    queue.pop();
    if (auto entry = get_entry(top_id)) {
      entry->rank = top_rank;
      if constexpr (requires { fetch_card(*entry); }) fetch_card(*entry);
      co_yield *entry;
    }
  }
  while (!queue.empty()) {
    const auto [id, rank] = queue.top();
    cursor.set(*reinterpret_cast<const uint64_t*>(&rank), id);
    queue.pop();
    if (auto entry = get_entry(id)) {
      entry->rank = rank;
      if constexpr (requires { fetch_card(*entry); }) fetch_card(*entry);
      co_yield *entry;
    }
  }
  cursor.reset();
}

template <class T, class Fn>
static inline auto ranked_new_comments(
  ReadTxn& txn,
  PageCursor& cursor,
  DBIter iter_by_new,
  Fn get_entry
) -> generator<const T&> {
  using IdTime = pair<uint64_t, Timestamp>;
  const auto from = cursor ? optional(uint_to_timestamp(cursor.k)) : nullopt;
  static constexpr auto id_time_cmp = [](const IdTime& a, const IdTime& b) -> bool { return a.second < b.second; };
  const auto now = now_t();
  const auto max_time = from.value_or(now);
  priority_queue<IdTime, vector<IdTime>, decltype(id_time_cmp)> queue;
  for (uint64_t id : iter_by_new) {
    const auto stats = txn.get_post_stats(id);
    if (!stats) continue;
    const auto timestamp = uint_to_timestamp(stats->get().latest_comment());
    if (timestamp >= max_time) continue;
    queue.emplace(id, timestamp);
    const auto [top_id, top_time] = queue.top();
    const auto max_possible_time = min(now, T::get_created_at(txn, id) + ACTIVE_COMMENT_MAX_AGE);
    if (max_possible_time > top_time) continue;
    cursor.set(timestamp_to_uint(top_time));
    queue.pop();
    if (auto entry = get_entry(top_id)) {
      if constexpr (requires { fetch_card(*entry); }) fetch_card(*entry);
      co_yield *entry;
    }
  }
  while (!queue.empty()) {
    const auto [id, timestamp] = queue.top();
    cursor.set(timestamp_to_uint(timestamp));
    queue.pop();
    if (auto entry = get_entry(id)) {
      if constexpr (requires { fetch_card(*entry); }) fetch_card(*entry);
      co_yield *entry;
    }
  }
  cursor.reset();
}

static auto comment_tree(
  ReadTxn& txn,
  CommentTree& tree,
  uint64_t parent,
  CommentSortType sort,
  Login login,
  OptRef<Thread> thread = {},
  bool is_thread_hidden = false,
  OptRef<Board> board = {},
  bool is_board_hidden = false,
  PageCursor from = {},
  uint16_t max_comments = 20,
  uint16_t max_depth = 5
) -> void {
  using enum CommentSortType;
  if (!max_depth || tree.size() >= max_comments) {
    tree.mark_continued(parent);
  } else if (sort == Hot) {
    PageCursor page_cursor = from;
    for (auto& entry : ranked<RankType::Hot, CommentDetail>(
      txn,
      page_cursor,
      txn.list_comments_of_post_new(parent),
      txn.list_comments_of_post_top(parent),
      [&](uint64_t id) -> optional<CommentDetail> {
        if (from && id == from.v) return {};
        const auto e = CommentDetail::get(
          txn, id, login, {}, false, thread, is_thread_hidden, board, is_board_hidden
        );
        return e.should_show(login) ? optional(e) : nullopt;
      },
      from.rank_k()
    )) {
      if (tree.size() >= max_comments) {
        tree.mark_continued(parent, PageCursor(entry.rank, entry.id));
        return;
      }
      const auto id = entry.id, children = entry.stats().child_count();
      tree.emplace(parent, entry);
      if (children) {
        comment_tree(
          txn, tree, id, sort, login,
          thread, is_thread_hidden, board, is_board_hidden,
          {}, max_comments, max_depth - 1
        );
      }
    }
    if (page_cursor) tree.mark_continued(parent, page_cursor);
  } else {
    DBIter iter = [&]{
      switch (sort) {
        case Hot: assert(false);
        case New: return txn.list_comments_of_post_new(parent, from.next_cursor_desc(parent));
        case Old: return txn.list_comments_of_post_old(parent, from.next_cursor_asc(parent));
        case Top: return txn.list_comments_of_post_top(parent, from.next_cursor_desc(parent));
      }
    }();
    for (auto id : iter) {
      if (tree.size() >= max_comments) {
        tree.mark_continued(parent, PageCursor(iter.get_cursor()->int_field_1(), id));
        return;
      }
      auto entry = CommentDetail::get(
        txn, id, login, {}, false, thread, is_thread_hidden, board, is_board_hidden
      );
      if (!entry.should_show(login)) continue;
      const auto children = entry.stats().child_count();
      tree.emplace(parent, entry);
      if (children) {
        comment_tree(
          txn, tree, id, sort, login,
          thread, is_thread_hidden, board, is_board_hidden,
          {}, max_comments, max_depth - 1
        );
      }
    }
    if (!iter.is_done()) {
      tree.mark_continued(parent, PageCursor(iter.get_cursor()->int_field_1(), *iter));
    }
  }
}

static inline auto earliest_time(SortType sort) -> Timestamp {
  using namespace chrono;
  using enum SortType;
  const auto now = now_t();
  switch (sort) {
    case TopYear: return now - 24h * 365;
    case TopSixMonths: return now - 24h * 30 * 6;
    case TopThreeMonths: return now - 24h * 30 * 3;
    case TopMonth: return now - 24h * 30;
    case TopWeek: return now - 24h * 7;
    case TopDay: return now - 24h;
    case TopTwelveHour: return now - 12h;
    case TopSixHour: return now - 6h;
    case TopHour: return now - 1h;
    default: return Timestamp::min();
  }
}

template <class T, class Fn>
static inline auto iter_gen(Fn get_entry, PageCursor& cursor, SortType sort, DBIter iter) -> generator<const T&> {
  const auto earliest = earliest_time(sort);
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto id = *it;
    if (++it == iter.end()) cursor.reset();
    else cursor.set(iter.get_cursor()->int_field_1(), *it);
    try {
      if (const optional<T> entry = get_entry(id)) {
        const Timestamp time = entry->created_at();
        if (time < earliest) continue;
        if constexpr (requires { fetch_card(*entry); }) fetch_card(*entry);
        co_yield *entry;
      }
    } catch (const ApiError& e) {
      spdlog::warn("{} {:x} error: {}", T::noun, id, e.what());
    }
  }
  cursor.reset();
}

auto PostController::thread_detail(
  ReadTxn& txn,
  CommentTree& tree_out,
  uint64_t id,
  CommentSortType sort,
  Login login,
  PageCursor from,
  uint16_t limit
) -> ThreadDetail {
  auto t = ThreadDetail::get(txn, id, login);
  if (!t.can_view(login)) throw ApiError("Cannot view this thread", 403);
  fetch_card(t);
  comment_tree(
    txn, tree_out, id, sort, login,
    t.thread(), t.hidden,
    t.board(), t.board_hidden,
    from, limit
  );
  return t;
}

auto PostController::comment_detail(
  ReadTxn& txn,
  CommentTree& tree_out,
  uint64_t id,
  CommentSortType sort,
  Login login,
  PageCursor from,
  uint16_t limit
) -> CommentDetail {
  auto c = CommentDetail::get(txn, id, login);
  if (!c.can_view(login)) throw ApiError("Cannot view this comment", 403);
  comment_tree(
    txn, tree_out, id, sort, login,
    c.thread(), c.thread_hidden,
    c.board(), c.board_hidden,
    from, limit
  );
  spdlog::info("comment_detail limit={:d} id={:d} parent={:d} tree_size={:d}",
    limit, id, c.comment().parent(), tree_out.size());
  return c;
}

static inline auto new_comments_cursor(
  PageCursor& from,
  optional<uint64_t> first_k = {}
) -> optional<pair<Cursor, uint64_t>> {
  using namespace chrono;
  if (!from) return {};
  const auto time = (uint64_t)duration_cast<seconds>(
    (uint_to_timestamp(from.k) - ACTIVE_COMMENT_MAX_AGE).time_since_epoch()
  ).count();
  return pair(first_k ? Cursor(*first_k, time) : Cursor(time), from.v);
}

auto PostController::list_board_threads(
  ReadTxn& txn,
  PageCursor& cursor,
  uint64_t board_id,
  SortType sort,
  Login login
) -> generator<const ThreadDetail&> {
  using namespace chrono;
  using enum SortType;
  const auto board = txn.get_board(board_id);
  if (!board) throw ApiError(format("Board {:x} does not exist"_cf, board_id), 410);
  const auto get_entry = [=, &txn](uint64_t id) -> optional<ThreadDetail> {
    const auto e = optional(ThreadDetail::get(txn, id, login, {}, false, board, false));
    return e->should_show(login) ? e : nullopt;
  };
  switch (sort) {
    case Active:
      return ranked<RankType::Active, ThreadDetail>(
        txn,
        cursor,
        txn.list_threads_of_board_new(board_id),
        txn.list_threads_of_board_top(board_id),
        get_entry,
        cursor.rank_k()
      );
    case Hot:
      return ranked<RankType::Hot, ThreadDetail>(
        txn,
        cursor,
        txn.list_threads_of_board_new(board_id),
        txn.list_threads_of_board_top(board_id),
        get_entry,
        cursor.rank_k()
      );
    case NewComments:
      return ranked_new_comments<ThreadDetail>(
        txn,
        cursor,
        txn.list_threads_of_board_new(board_id, new_comments_cursor(cursor, board_id)),
        get_entry
      );
      case New:
        return iter_gen<ThreadDetail>(get_entry, cursor, sort,
          txn.list_threads_of_board_new(board_id, cursor.next_cursor_asc(board_id)));
      case Old:
        return iter_gen<ThreadDetail>(get_entry, cursor, sort,
          txn.list_threads_of_board_old(board_id, cursor.next_cursor_desc(board_id)));
      case MostComments:
        return iter_gen<ThreadDetail>(get_entry, cursor, sort,
          txn.list_threads_of_board_most_comments(board_id, cursor.next_cursor_asc(board_id)));
      case TopAll:
      case TopYear:
      case TopSixMonths:
      case TopThreeMonths:
      case TopMonth:
      case TopWeek:
      case TopDay:
      case TopTwelveHour:
      case TopSixHour:
      case TopHour:
        return iter_gen<ThreadDetail>(get_entry, cursor, sort,
          txn.list_threads_of_board_top(board_id, cursor.next_cursor_asc(board_id)));
  }
}

auto PostController::list_board_comments(
  ReadTxn& txn,
  PageCursor& cursor,
  uint64_t board_id,
  SortType sort,
  Login login
) -> generator<const CommentDetail&> {
  using enum SortType;
  const auto board = txn.get_board(board_id);
  if (!board) throw ApiError(format("Board {:x} does not exist"_cf, board_id), 410);
  const auto get_entry = [=, &txn](uint64_t id) -> optional<CommentDetail> {
    const auto e = optional(CommentDetail::get(txn, id, login, {}, false, {}, false, board, false));
    return e->should_show(login) ? e : nullopt;
  };
  switch (sort) {
    case Active:
      return ranked<RankType::Active, CommentDetail>(
        txn,
        cursor,
        txn.list_comments_of_board_new(board_id),
        txn.list_comments_of_board_top(board_id),
        get_entry,
        cursor.rank_k()
      );
    case Hot:
      return ranked<RankType::Hot, CommentDetail>(
        txn,
        cursor,
        txn.list_comments_of_board_new(board_id),
        txn.list_comments_of_board_top(board_id),
        get_entry,
        cursor.rank_k()
      );
    case NewComments:
      return ranked_new_comments<CommentDetail>(
        txn,
        cursor,
        txn.list_comments_of_board_new(board_id, new_comments_cursor(cursor, board_id)),
        get_entry
      );
    case New:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_of_board_new(board_id, cursor.next_cursor_asc(board_id)));
    case Old:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_of_board_old(board_id, cursor.next_cursor_desc(board_id)));
    case MostComments:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_of_board_most_comments(board_id, cursor.next_cursor_asc(board_id)));
    case TopAll:
    case TopYear:
    case TopSixMonths:
    case TopThreeMonths:
    case TopMonth:
    case TopWeek:
    case TopDay:
    case TopTwelveHour:
    case TopSixHour:
    case TopHour:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_of_board_top(board_id, cursor.next_cursor_asc(board_id)));
  }
}

template <class T>
auto feed_filter_fn(uint64_t feed_id, ReadTxn& txn, Login login) -> function<bool (const T&)> {
  switch (feed_id) {
    case PostController::FEED_ALL:
      return [&](auto& e) { return e.should_show(login); };
    case PostController::FEED_LOCAL:
      return [&](auto& e) { return !e.board().instance() && e.should_show(login); };
    case PostController::FEED_HOME: {
      if (!login) throw ApiError("Must be logged in to view Home feed", 403);
      phmap::flat_hash_set<uint64_t> subs;
      for (const auto id : txn.list_subscribed_boards(login->id)) subs.insert(id);
      return [subs = std::move(subs), &login](auto& e) {
        return subs.contains(e.thread().board()) && e.should_show(login);
      };
    }
    default:
      throw ApiError(format("No feed with ID {:x}"_cf, feed_id), 410);
  }
}

auto PostController::list_feed_threads(
  ReadTxn& txn,
  PageCursor& cursor,
  uint64_t feed_id,
  SortType sort,
  Login login
) -> generator<const ThreadDetail&> {
  using enum SortType;
  auto filter_thread = feed_filter_fn<ThreadDetail>(feed_id, txn, login);
  const auto get_entry = [=, &txn](uint64_t id) -> optional<ThreadDetail> {
    const auto e = optional(ThreadDetail::get(txn, id, login));
    return filter_thread(*e) ? e : nullopt;
  };
  switch (sort) {
    case Active:
      return ranked<RankType::Active, ThreadDetail>(
        txn,
        cursor,
        txn.list_threads_new(),
        txn.list_threads_top(),
        get_entry,
        cursor.rank_k()
      );
    case Hot:
      return ranked<RankType::Hot, ThreadDetail>(
        txn,
        cursor,
        txn.list_threads_new(),
        txn.list_threads_top(),
        get_entry,
        cursor.rank_k()
      );
    case NewComments:
      return ranked_new_comments<ThreadDetail>(
        txn,
        cursor,
        txn.list_threads_new(new_comments_cursor(cursor)),
        get_entry
      );
    case New:
      return iter_gen<ThreadDetail>(get_entry, cursor, sort,
        txn.list_threads_new(cursor.next_cursor_asc()));
    case Old:
      return iter_gen<ThreadDetail>(get_entry, cursor, sort,
        txn.list_threads_old(cursor.next_cursor_desc()));
    case MostComments:
      return iter_gen<ThreadDetail>(get_entry, cursor, sort,
        txn.list_threads_most_comments(cursor.next_cursor_asc()));
    case TopAll:
    case TopYear:
    case TopSixMonths:
    case TopThreeMonths:
    case TopMonth:
    case TopWeek:
    case TopDay:
    case TopTwelveHour:
    case TopSixHour:
    case TopHour:
      return iter_gen<ThreadDetail>(get_entry, cursor, sort,
        txn.list_threads_top(cursor.next_cursor_asc()));
  }
}

auto PostController::list_feed_comments(
  ReadTxn& txn,
  PageCursor& cursor,
  uint64_t feed_id,
  SortType sort,
  Login login
) -> generator<const CommentDetail&> {
  using enum SortType;
  auto filter_comment = feed_filter_fn<CommentDetail>(feed_id, txn, login);
  const auto get_entry = [=, &txn](uint64_t id) -> optional<CommentDetail> {
    const auto e = optional(CommentDetail::get(txn, id, login));
    return filter_comment(*e) ? e : nullopt;
  };
  switch (sort) {
    case Active:
      return ranked<RankType::Active, CommentDetail>(
        txn,
        cursor,
        txn.list_comments_new(),
        txn.list_comments_top(),
        get_entry,
        cursor.rank_k()
      );
    case Hot:
      return ranked<RankType::Hot, CommentDetail>(
        txn,
        cursor,
        txn.list_comments_new(),
        txn.list_comments_top(),
        get_entry,
        cursor.rank_k()
      );
    case NewComments:
      return ranked_new_comments<CommentDetail>(
        txn,
        cursor,
        txn.list_comments_new(new_comments_cursor(cursor)),
        get_entry
      );
    case New:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_new(cursor.next_cursor_asc()));
    case Old:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_old(cursor.next_cursor_desc()));
    case MostComments:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_most_comments(cursor.next_cursor_asc()));
    case TopAll:
    case TopYear:
    case TopSixMonths:
    case TopThreeMonths:
    case TopMonth:
    case TopWeek:
    case TopDay:
    case TopTwelveHour:
    case TopSixHour:
    case TopHour:
      return iter_gen<CommentDetail>(get_entry, cursor, sort,
        txn.list_comments_top(cursor.next_cursor_asc()));
  }
}

auto PostController::list_user_threads(
  ReadTxn& txn,
  PageCursor& cursor,
  uint64_t user_id,
  UserPostSortType sort,
  Login login
) -> generator<const ThreadDetail&> {
  const auto user = txn.get_user(user_id);
  if (!user) throw ApiError(format("User {:x} does not exist"_cf, user_id), 410);
  DBIter iter = [&]{
    using enum UserPostSortType;
    switch (sort) {
      case New: return txn.list_threads_of_user_new(user_id, cursor.next_cursor_asc(user_id));
      case Old: return txn.list_threads_of_user_old(user_id, cursor.next_cursor_desc(user_id));
      case Top: return txn.list_threads_of_user_top(user_id, cursor.next_cursor_asc(user_id));
    }
  }();
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto thread_id = *it;
    if (++it == iter.end()) cursor.reset();
    else cursor.set(iter.get_cursor()->int_field_1(), *it);
    try {
      const auto entry = ThreadDetail::get(txn, thread_id, login, user);
      if (!entry.should_show(login)) continue;
      fetch_card(entry);
      co_yield entry;
    } catch (const ApiError& e) {
      spdlog::warn("Thread {:x} error: {}", thread_id, e.what());
    }
  }
  cursor.reset();
}

auto PostController::list_user_comments(
  ReadTxn& txn,
  PageCursor& cursor,
  uint64_t user_id,
  UserPostSortType sort,
  Login login
) -> generator<const CommentDetail&> {
  DBIter iter = [&]{
    using enum UserPostSortType;
    switch (sort) {
      case New: return txn.list_comments_of_user_new(user_id, cursor.next_cursor_asc(user_id));
      case Old: return txn.list_comments_of_user_old(user_id, cursor.next_cursor_desc(user_id));
      case Top: return txn.list_comments_of_user_top(user_id, cursor.next_cursor_asc(user_id));
    }
  }();
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto comment_id = *it;
    if (++it == iter.end()) cursor.reset();
    else cursor.set(iter.get_cursor()->int_field_1(), *it);
    try {
      const auto entry = CommentDetail::get(txn, comment_id, login);
      if (!entry.should_show(login)) continue;
      co_yield entry;
    } catch (const ApiError& e) {
      spdlog::warn("Comment {:x} error: {}", comment_id, e.what());
    }
  }
  cursor.reset();
}

auto PostController::create_thread(
  WriteTxn& txn,
  uint64_t author,
  uint64_t board,
  optional<string_view> remote_post_url,
  optional<string_view> remote_activity_url,
  Timestamp created_at,
  optional<Timestamp> updated_at,
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
  if (!submission_url && !text_content_markdown) {
    throw ApiError("Post must contain either a submission URL or text content", 400);
  }
  const auto site = site_controller->site_detail();
  if (text_content_markdown) {
    auto len = text_content_markdown->length();
    if (len > site->post_max_length) {
      throw ApiError(format("Post text content cannot be larger than {:d} bytes"_cf, site->post_max_length), 400);
    } else if (len < 1) {
      text_content_markdown = {};
    }
  }
  auto len = title.length();
  if (len > 1024) throw ApiError("Post title cannot be longer than 1024 bytes", 400);
  else if (len < 1) throw ApiError("Post title cannot be blank", 400);
  union { uint8_t bytes[4]; uint32_t n; } salt;
  RAND_pseudo_bytes(salt.bytes, 4);
  auto user = txn.get_user(author);
  if (!user) throw ApiError(format("User {:x} does not exist"_cf, author), 400);
  FlatBufferBuilder fbb;
  const auto submission_s = submission_url ? fbb.CreateString(*submission_url) : 0,
    content_raw_s = text_content_markdown ? fbb.CreateString(*text_content_markdown) : 0,
    content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0,
    remote_post_url_s = remote_post_url ? fbb.CreateString(*remote_post_url) : 0,
    remote_activity_url_s = remote_activity_url ? fbb.CreateString(*remote_activity_url) : 0;
  auto [title_blocks_type, title_blocks] = plain_text_with_emojis_to_rich_text(fbb, title);
  RichTextVectors content;
  phmap::flat_hash_set<uint64_t> to_notify;
  if (text_content_markdown) {
    content = markdown_to_rich_text(fbb, *text_content_markdown);
    const auto& content_type_vec = *get_temporary_pointer(fbb, content.first);
    const auto& content_vec = *get_temporary_pointer(fbb, content.second);
    for (unsigned i = 0; i < content_type_vec.size(); i++) {
      if (content_type_vec[i] != RichText::UserLink) continue;
      if (const auto id = txn.get_user_id_by_name(content_vec.GetAsString(i)->string_view())) {
        const auto mentioned = txn.get_user(*id);
        if (mentioned && !mentioned->get().instance()) to_notify.emplace(*id);
      }
    }
  }
  ThreadBuilder b(fbb);
  b.add_created_at(timestamp_to_uint(created_at));
  if (updated_at) b.add_updated_at(timestamp_to_uint(*updated_at));
  b.add_author(author);
  b.add_board(board);
  b.add_title_type(title_blocks_type);
  b.add_title(title_blocks);
  b.add_salt(salt.n);
  if (user->get().instance()) {
    if (!remote_post_url || !remote_activity_url) {
      throw ApiError("Post from remote user must have URL and activity URL", 400);
    }
    b.add_instance(user->get().instance());
    b.add_original_post_url(remote_post_url_s);
    b.add_activity_url(remote_activity_url_s);
  }
  if (submission_url) b.add_content_url(submission_s);
  if (text_content_markdown) {
    b.add_content_text_raw(content_raw_s);
    b.add_content_text_type(content.first);
    b.add_content_text(content.second);
  }
  if (content_warning) b.add_content_warning(content_warning_s);
  fbb.Finish(b.Finish());
  uint64_t thread_id = txn.create_thread(fbb.GetBufferSpan());
  const auto new_thread = ThreadDetail::get(txn, thread_id, {});
  for (const auto user : to_notify) {
    if (!new_thread.should_show(LocalUserDetail::get(txn, user, {}))) {
      continue;
    }
    fbb.Clear();
    NotificationBuilder b(fbb);
    b.add_type(NotificationType::MentionInThread);
    b.add_user(user);
    b.add_created_at(timestamp_to_uint(created_at));
    b.add_subject(thread_id);
    fbb.Finish(b.Finish());
    txn.create_notification(fbb.GetBufferSpan());
    txn.queue_event(event_bus, Event::Notification, user);
  }

  txn.queue_event(event_bus, Event::UserStatsUpdate, author);
  txn.queue_event(event_bus, Event::BoardStatsUpdate, board);
  txn.queue_event(event_bus, Event::ThreadUpdate, thread_id);
  return thread_id;
}

auto PostController::create_local_thread(
  WriteTxn& txn,
  uint64_t author,
  uint64_t board,
  string_view title,
  optional<string_view> submission_url,
  optional<string_view> text_content_markdown,
  optional<string_view> content_warning
) -> uint64_t {
  const auto login = LocalUserDetail::get_login(txn, author);
  if (!BoardDetail::get(txn, board, login).can_create_thread(login)) {
    throw ApiError("User cannot create threads in this board", 403);
  }
  const auto thread_id = create_thread(
    txn, author, board, {}, {}, now_t(), {},
    title, submission_url, text_content_markdown, content_warning
  );
  txn.set_vote(author, thread_id, Vote::Upvote);
  return thread_id;
}

auto PostController::update_thread(
  WriteTxn& txn,
  uint64_t id,
  optional<uint64_t> as_user,
  const ThreadUpdate& update
) -> void {
  const auto login = LocalUserDetail::get_login(txn, as_user);
  const auto detail = ThreadDetail::get(txn, id, login);
  if (login && detail.thread().instance()) {
    throw ApiError("Cannot edit a thread from a different instance", 403);
  }
  if (login && !detail.can_edit(login)) {
    throw ApiError("User does not have permission to edit this thread", 403);
  }
  if (update.title && update.title->empty()) {
    throw ApiError("Title cannot be empty", 400);
  }
  FlatBufferBuilder fbb;
  fbb.Finish(patch_thread(fbb, detail.thread(), {
    .title = update.title,
    .content_text = update.text_content,
    .content_warning = update.content_warning,
    .updated_at = now_s()
  }));
  txn.set_thread(id, fbb.GetBufferSpan());
  txn.queue_event(event_bus, Event::ThreadUpdate, id);
}

auto PostController::create_comment(
  WriteTxn& txn,
  uint64_t author,
  uint64_t parent,
  optional<string_view> remote_post_url,
  optional<string_view> remote_activity_url,
  Timestamp created_at,
  optional<Timestamp> updated_at,
  string_view text_content_markdown,
  optional<string_view> content_warning,
  Login login
) -> uint64_t {
  optional<ThreadDetail> parent_thread;
  optional<CommentDetail> parent_comment;
  try {
    parent_thread = ThreadDetail::get(txn, parent, login);
  } catch (...) {
    parent_comment = CommentDetail::get(txn, parent, login);
    parent_thread = ThreadDetail::get(txn, parent_comment->comment().thread(), login);
  }
  if (login && (parent_comment ? !parent_comment->can_reply_to(login) : !parent_thread->can_reply_to(login))) {
    throw ApiError("User cannot reply to this post", 403);
  }
  if (text_content_markdown.empty()) {
    throw ApiError("Comment text content cannot be blank", 400);
  }
  const auto site = site_controller->site_detail();
  if (text_content_markdown.length() > site->remote_post_max_length) {
    throw ApiError(format("Comment text content cannot be larger than {:d} bytes"_cf, site->remote_post_max_length), 400);
  }
  union { uint8_t bytes[4]; uint32_t n; } salt;
  RAND_pseudo_bytes(salt.bytes, 4);
  auto user = txn.get_user(author);
  if (!user) throw ApiError(format("User {:x} does not exist"_cf, author), 400);
  FlatBufferBuilder fbb;
  const auto content_raw_s = fbb.CreateString(text_content_markdown),
    content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0,
    remote_post_url_s = remote_post_url ? fbb.CreateString(*remote_post_url) : 0,
    remote_activity_url_s = remote_activity_url ? fbb.CreateString(*remote_activity_url) : 0;
  const auto [content_type, content] = markdown_to_rich_text(fbb, text_content_markdown);

  phmap::flat_hash_set<pair<uint64_t, NotificationType>> to_notify;
  uint64_t already_notified = 0;
  if (parent_comment && !parent_comment->author().instance()) {
    already_notified = parent_comment->author_id();
    to_notify.emplace(parent_comment->author_id(), NotificationType::ReplyToComment);
  }
  if (!parent_thread->author().instance() && parent_thread->author_id() != already_notified) {
    to_notify.emplace(parent_thread->author_id(), NotificationType::ReplyToThread);
  }
  const auto& content_type_vec = *get_temporary_pointer(fbb, content_type);
  const auto& content_vec = *get_temporary_pointer(fbb, content);
  for (unsigned i = 0; i < content_type_vec.size(); i++) {
    if (content_type_vec[i] != RichText::UserLink) continue;
    if (const auto id = txn.get_user_id_by_name(content_vec.GetAsString(i)->string_view())) {
      const auto mentioned = txn.get_user(*id);
      if (mentioned && !mentioned->get().instance()) {
        to_notify.emplace(*id, NotificationType::MentionInComment);
      }
    }
  }

  CommentBuilder b(fbb);
  b.add_created_at(timestamp_to_uint(created_at));
  if (updated_at) b.add_updated_at(timestamp_to_uint(*updated_at));
  b.add_author(author);
  b.add_parent(parent_comment ? parent_comment->id : parent_thread->id);
  b.add_thread(parent_thread->id);
  b.add_content_raw(content_raw_s);
  b.add_content_type(content_type);
  b.add_content(content);
  b.add_salt(salt.n);
  if (content_warning) b.add_content_warning(content_warning_s);
  if (user->get().instance()) {
    if (!remote_post_url || !remote_activity_url) {
      throw ApiError("Post from remote user must have URL and activity URL", 400);
    }
    b.add_instance(user->get().instance());
    b.add_original_post_url(remote_post_url_s);
    b.add_activity_url(remote_activity_url_s);
  }
  fbb.Finish(b.Finish());
  const auto comment_id = txn.create_comment(fbb.GetBufferSpan());
  const auto board_id = parent_thread->thread().board();
  const auto new_comment = CommentDetail::get(txn, comment_id, {});
  for (const auto [user, type] : to_notify) {
    if (!new_comment.should_show(LocalUserDetail::get(txn, user, {}))) {
      continue;
    }
    fbb.Clear();
    NotificationBuilder b(fbb);
    b.add_type(type);
    b.add_user(user);
    b.add_created_at(timestamp_to_uint(created_at));
    b.add_subject(comment_id);
    fbb.Finish(b.Finish());
    txn.create_notification(fbb.GetBufferSpan());
    txn.queue_event(event_bus, Event::Notification, user);
  }

  txn.queue_event(event_bus, Event::UserStatsUpdate, author);
  txn.queue_event(event_bus, Event::BoardStatsUpdate, board_id);
  txn.queue_event(event_bus, Event::PostStatsUpdate, parent_thread->id);
  txn.queue_event(event_bus, Event::CommentUpdate, comment_id);
  if (parent_comment) txn.queue_event(event_bus, Event::PostStatsUpdate, parent_comment->id);
  return comment_id;
}

auto PostController::create_local_comment(
  WriteTxn& txn,
  uint64_t author,
  uint64_t parent,
  string_view text_content_markdown,
  optional<string_view> content_warning
) -> uint64_t {
  const auto login = LocalUserDetail::get_login(txn, author);
  const auto comment_id = create_comment(
    txn, author, parent, {}, {},
    now_t(), {}, text_content_markdown, content_warning, login
  );
  txn.set_vote(author, comment_id, Vote::Upvote);
  return comment_id;
}

auto PostController::update_comment(
  WriteTxn& txn,
  uint64_t id,
  optional<uint64_t> as_user,
  const CommentUpdate& update
) -> void {
  const auto login = LocalUserDetail::get_login(txn, as_user);
  const auto detail = CommentDetail::get(txn, id, login);
  if (login && detail.comment().instance()) {
    throw ApiError("Cannot edit a comment from a different instance", 403);
  }
  if (login && !detail.can_edit(login)) {
    throw ApiError("User does not have permission to edit this comment", 403);
  }
  if (update.text_content && update.text_content->empty()) {
    throw ApiError("Content cannot be empty", 400);
  }
  FlatBufferBuilder fbb;
  fbb.Finish(patch_comment(fbb, detail.comment(), {
    .content = update.text_content,
    .content_warning = update.content_warning,
    .updated_at = now_s()
  }));
  txn.set_comment(id, fbb.GetBufferSpan());
  txn.queue_event(event_bus, Event::CommentUpdate, id);
}

auto PostController::vote(
  WriteTxn& txn,
  uint64_t user_id,
  uint64_t post_id,
  Vote vote
) -> void {
  if (!txn.get_user(user_id)) throw ApiError(format("User {:x} does not exist"_cf, user_id), 410);
  const auto thread = txn.get_thread(post_id);
  const auto comment = !thread ? txn.get_comment(post_id) : nullopt;
  if (!thread && !comment) throw ApiError(format("Post {:x} does not exist"_cf, post_id), 410);
  const auto op = thread ? thread->get().author() : comment->get().author();
  txn.set_vote(user_id, post_id, vote);

  txn.queue_event(event_bus, Event::UserStatsUpdate, op);
  txn.queue_event(event_bus, Event::PostStatsUpdate, post_id);
}

}