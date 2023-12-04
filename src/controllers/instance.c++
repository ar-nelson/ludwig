#include "instance.h++"
#include <mutex>
#include <regex>
#include <queue>
#include <duthomhas/csprng.hpp>
#include <openssl/evp.h>
#include "util/web.h++"
#include "util/lambda_macros.h++"

using std::function, std::min, std::nullopt, std::optional, std::pair,
    std::prev, std::regex, std::regex_match, std::shared_ptr, std::string,
    std::string_view, std::tuple, std::vector, flatbuffers::Offset,
    flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot, flatbuffers::Vector;
namespace chrono = std::chrono;

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

  static inline auto rank_numerator(int64_t karma) -> double {
    return std::log(std::max<int64_t>(1, 3 + karma));
  }
  static inline auto rank_denominator(chrono::duration<double> time_diff) -> double {
    return std::pow(std::max(0L, chrono::duration_cast<chrono::hours>(time_diff).count()) + 2L, RANK_GRAVITY);
  }
  template <typename T> static auto latest_comment_cmp(const T& a, const T& b) -> bool {
    return a.stats().latest_comment() > b.stats().latest_comment();
  }
  using RankedId = pair<uint64_t, double>;
  static auto ranked_id_cmp(const RankedId& a, const RankedId& b) -> bool {
    return a.second < b.second;
  }

  using RankedQueue = std::priority_queue<RankedId, vector<RankedId>, decltype(ranked_id_cmp)*>;

  template <class T, size_t PageSize>
  static inline auto finish_ranked_queue(
    stlpb::static_vector<T, PageSize>& entries,
    RankedQueue& queue,
    function<optional<T> (uint64_t)>& get_entry
  ) -> PageCursor {
    while (!queue.empty()) {
      const auto [id, rank] = queue.top();
      queue.pop();
      if (auto entry = get_entry(id)) {
        if (entries.full()) {
          return PageCursor(prev(entries.end())->rank, id);
        } else {
          entry->rank = rank;
          entries.push_back(*entry);
        }
      }
    }
    return {};
  }

  template <class T> static inline auto get_created_at(ReadTxnBase& txn, uint64_t id) -> chrono::system_clock::time_point;
  template <> inline auto get_created_at<ThreadDetail>(ReadTxnBase& txn, uint64_t id) -> chrono::system_clock::time_point {
    const auto thread = txn.get_thread(id);
    if (!thread) return chrono::system_clock::time_point::min();
    return chrono::system_clock::time_point(chrono::seconds(thread->get().created_at()));
  }
  template <> inline auto get_created_at<CommentDetail>(ReadTxnBase& txn, uint64_t id) -> chrono::system_clock::time_point {
    const auto comment = txn.get_comment(id);
    if (!comment) return chrono::system_clock::time_point::min();
    return chrono::system_clock::time_point(chrono::seconds(comment->get().created_at()));
  }

  template <class T, size_t PageSize = ITEMS_PER_PAGE>
  static inline auto ranked_active(
    stlpb::static_vector<T, PageSize>& entries,
    ReadTxnBase& txn,
    DBIter iter_by_new,
    DBIter iter_by_top,
    function<optional<T> (uint64_t)> get_entry,
    double max_rank = INFINITY
  ) -> PageCursor {
    using namespace chrono;
    int64_t max_possible_karma;
    if (iter_by_top.is_done() || iter_by_new.is_done()) return {};
    if (auto top_stats = txn.get_post_stats(*iter_by_top)) {
      max_possible_karma = top_stats->get().karma();
    } else return {};
    const auto max_possible_numerator = rank_numerator(max_possible_karma);
    const auto now = system_clock::now();
    RankedQueue queue(ranked_id_cmp);
    for (auto id : iter_by_new) {
      const auto stats = txn.get_post_stats(id);
      if (!stats) continue;
      const system_clock::time_point timestamp(seconds(stats->get().latest_comment()));
      const auto denominator = rank_denominator(now - timestamp);
      const auto rank = rank_numerator(stats->get().karma()) / denominator;
      if (rank >= max_rank) continue;
      queue.emplace(id, rank);
      const auto latest_possible_timestamp = std::min(now, get_created_at<T>(txn, id) + ACTIVE_COMMENT_MAX_AGE);
      const auto [top_id, top_rank] = queue.top();
      const double
        min_possible_denominator =
          rank_denominator(latest_possible_timestamp < now ? now - latest_possible_timestamp : seconds::zero()),
        max_possible_rank = max_possible_numerator / min_possible_denominator;
      if (max_possible_rank > top_rank) continue;
      queue.pop();
      if (auto entry = get_entry(top_id)) {
        entry->rank = top_rank;
        entries.push_back(*entry);
        if (entries.full()) break;
      }
    }
    return finish_ranked_queue(entries, queue, get_entry);
  }

  template <class T, size_t PageSize = ITEMS_PER_PAGE>
  static inline auto ranked_hot(
    stlpb::static_vector<T, PageSize>& entries,
    ReadTxnBase& txn,
    DBIter iter_by_new,
    DBIter iter_by_top,
    function<optional<T> (uint64_t)> get_entry,
    double max_rank = INFINITY
  ) -> PageCursor {
    using namespace chrono;
    int64_t max_possible_karma;
    if (iter_by_top.is_done() || iter_by_new.is_done()) return {};
    if (auto top_stats = txn.get_post_stats(*iter_by_top)) {
      max_possible_karma = top_stats->get().karma();
    } else return {};
    const auto max_possible_numerator = rank_numerator(max_possible_karma);
    const auto now = system_clock::now();
    RankedQueue queue(ranked_id_cmp);
    for (auto id : iter_by_new) {
      const auto timestamp = get_created_at<T>(txn, id);
      const auto stats = txn.get_post_stats(id);
      if (!stats) continue;
      const auto denominator = rank_denominator(now - timestamp);
      const auto rank = rank_numerator(stats->get().karma()) / denominator;
      if (rank >= max_rank) continue;
      queue.emplace(id, rank);
      const auto [top_id, top_rank] = queue.top();
      const double max_possible_rank = max_possible_numerator / denominator;
      if (max_possible_rank > top_rank) continue;
      queue.pop();
      if (auto entry = get_entry(top_id)) {
        entry->rank = top_rank;
        entries.push_back(*entry);
        if (entries.full()) break;
      }
    }
    return finish_ranked_queue(entries, queue, get_entry);
  }

  template <class T, size_t PageSize = ITEMS_PER_PAGE>
  static inline auto ranked_new_comments(
    stlpb::static_vector<T, PageSize>& entries,
    ReadTxnBase& txn,
    DBIter iter_by_new,
    function<optional<T> (uint64_t)> get_entry,
    optional<chrono::system_clock::time_point> from = {}
  ) -> PageCursor {
    using namespace chrono;
    using IdTime = pair<uint64_t, system_clock::time_point>;
    static constexpr auto id_time_cmp = [](const IdTime& a, const IdTime& b) -> bool { return a.second < b.second; };
    const auto now = system_clock::now();
    const auto max_time = from.value_or(now);
    std::priority_queue<IdTime, vector<IdTime>, decltype(id_time_cmp)> queue;
    for (uint64_t id : iter_by_new) {
      const auto stats = txn.get_post_stats(id);
      if (!stats) continue;
      const system_clock::time_point timestamp(seconds(stats->get().latest_comment()));
      if (timestamp >= max_time) continue;
      queue.emplace(id, timestamp);
      const auto [top_id, top_time] = queue.top();
      const auto max_possible_time = std::min(now, get_created_at<T>(txn, id) + ACTIVE_COMMENT_MAX_AGE);
      if (max_possible_time > top_time) continue;
      queue.pop();
      if (auto entry = get_entry(top_id)) {
        entries.push_back(*entry);
        if (entries.full()) break;
      }
    }
    while (!queue.empty()) {
      const auto [id, timestamp] = queue.top();
      queue.pop();
      if (auto entry = get_entry(id)) {
        if (entries.full()) return PageCursor(prev(entries.end())->stats().latest_comment());
        else entries.push_back(*entry);
      }
    }
    return {};
  }

  static auto comment_tree(
    ReadTxnBase& txn,
    CommentTree& tree,
    uint64_t parent,
    CommentSortType sort,
    Login login,
    OptRef<Thread> thread = {},
    bool is_thread_hidden = false,
    OptRef<Board> board = {},
    bool is_board_hidden = false,
    PageCursor from = {},
    size_t max_comments = ITEMS_PER_PAGE,
    size_t max_depth = 5
  ) -> void {
    if (!max_depth) {
      tree.mark_continued(parent);
      return;
    }
    if (tree.size() >= max_comments) return;
    optional<DBIter> iter;
    switch (sort) {
      case CommentSortType::Hot: {
        // TODO: Truncate when running out of total comments
        stlpb::static_vector<CommentDetail, ITEMS_PER_PAGE> entries;
        auto page_cursor = ranked_hot<CommentDetail, ITEMS_PER_PAGE>(
          entries,
          txn,
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
        );
        for (auto entry : entries) {
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
        return;
      }
      case CommentSortType::New:
        iter.emplace(txn.list_comments_of_post_new(parent, from.next_cursor_desc(parent)));
        break;
      case CommentSortType::Old:
        iter.emplace(txn.list_comments_of_post_old(parent, from.next_cursor_asc(parent)));
        break;
      case CommentSortType::Top:
        iter.emplace(txn.list_comments_of_post_top(parent, from.next_cursor_desc(parent)));
        break;
    }
    assert(!!iter);
    for (auto id : *iter) {
      if (tree.size() >= max_comments) {
        tree.mark_continued(parent, PageCursor(iter->get_cursor()->int_field_1(), id));
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
    if (!iter->is_done()) {
      tree.mark_continued(parent, PageCursor(iter->get_cursor()->int_field_1(), **iter));
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
    shared_ptr<RichTextParser> rich_text,
    shared_ptr<EventBus> event_bus,
    optional<shared_ptr<SearchEngine>> search_engine
  ) : db(db), http_client(http_client), rich_text(rich_text), event_bus(event_bus), search_engine(search_engine) {
    auto txn = db->open_read_txn();
    auto detail = new SiteDetail;
    *detail = SiteDetail::get(txn);
    cached_site_detail = detail;
  }

  InstanceController::~InstanceController() {
    const SiteDetail* null = nullptr;
    auto ptr = cached_site_detail.exchange(null);
    if (ptr) delete ptr;
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

  auto InstanceController::can_change_site_settings(Login login) -> bool {
    return login && login->local_user().admin();
  }

  auto InstanceController::can_create_board(Login login) -> bool {
    return login && (!site_detail()->board_creation_admin_only || login->local_user().admin());
  }

  auto InstanceController::validate_or_regenerate_session(
    ReadTxnBase& txn,
    uint64_t session_id,
    string_view ip,
    string_view user_agent
  ) -> optional<LoginResponse> {
    using namespace chrono;
    const auto session_opt = txn.get_session(session_id);
    if (!session_opt) return {};
    const auto& session = session_opt->get();
    const auto user = session.user();
    if (session.remember() && system_clock::now() - system_clock::time_point(seconds(session.created_at())) >= hours(24)) {
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

    // Note that this returns 0 on success, 1 on failure!
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
    PageCursor from
  ) -> pair<ThreadDetail, CommentTree> {
    pair<ThreadDetail, CommentTree> p(ThreadDetail::get(txn, id, login), {});
    if (!p.first.can_view(login)) throw ApiError("Cannot view this thread", 403);
    if (p.first.should_fetch_card()) event_bus->dispatch(Event::ThreadFetchLinkCard, id);
    comment_tree(
      txn, p.second, id, sort, login,
      p.first.thread(), p.first.hidden,
      p.first.board(), p.first.board_hidden,
      from
    );
    return p;
  }
  auto InstanceController::comment_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    PageCursor from
  ) -> pair<CommentDetail, CommentTree> {
    pair<CommentDetail, CommentTree> p(CommentDetail::get(txn, id, login), {});
    if (!p.first.can_view(login)) throw ApiError("Cannot view this comment", 403);
    comment_tree(
      txn, p.second, id, sort, login,
      p.first.thread(), p.first.thread_hidden,
      p.first.board(), p.first.board_hidden,
      from
    );
    return p;
  }
  auto InstanceController::user_detail(ReadTxnBase& txn, uint64_t id, Login login) -> UserDetail {
    const auto detail = UserDetail::get(txn, id, login);
    if (!detail.can_view(login)) throw ApiError("Cannot view this user", 403);
    return detail;
  }
  auto InstanceController::local_user_detail(ReadTxnBase& txn, uint64_t id, Login login) -> LocalUserDetail {
    const auto detail = LocalUserDetail::get(txn, id, {});
    if (!detail.can_view(login)) throw ApiError("Cannot view this user", 403);
    return detail;
  }
  auto InstanceController::board_detail(ReadTxnBase& txn, uint64_t id, Login login) -> BoardDetail {
    const auto detail = BoardDetail::get(txn, id, {});
    if (!detail.can_view(login)) throw ApiError("Cannot view this board", 403);
    return detail;
  }
  auto InstanceController::local_board_detail(ReadTxnBase& txn, uint64_t id, Login login) -> LocalBoardDetail {
    const auto detail = LocalBoardDetail::get(txn, id, {});
    if (!detail.can_view(login)) throw ApiError("Cannot view this board", 403);
    return detail;
  }
  auto InstanceController::list_users(
    ReadTxnBase& txn,
    UserSortType sort,
    bool local_only,
    Login login,
    PageCursor from
  ) -> PageOf<UserDetail> {
    PageOf<UserDetail> out { {}, !from, {} };
    optional<DBIter> iter;
    switch (sort) {
      case UserSortType::New:
        iter.emplace(txn.list_users_new(from.next_cursor_desc()));
        break;
      case UserSortType::Old:
        iter.emplace(txn.list_users_old(from.next_cursor_asc()));
        break;
      case UserSortType::NewPosts:
        iter.emplace(txn.list_users_new_posts(from.next_cursor_desc()));
        break;
      case UserSortType::MostPosts:
        iter.emplace(txn.list_users_most_posts(from.next_cursor_desc()));
        break;
    }
    assert(!!iter);
    for (const auto id : *iter) {
      try {
        const auto d = UserDetail::get(txn, id, login);
        if (local_only && d.user().instance()) continue;
        if (!d.should_show(login)) continue;
        out.entries.push_back(d);
      } catch (const ApiError& e) {
        spdlog::warn("User {:x} error: {}", id, e.what());
      }
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) {
      out.next = PageCursor(iter->get_cursor()->int_field_0(), **iter);
    }
    return out;
  }
  auto InstanceController::list_boards(
    ReadTxnBase& txn,
    BoardSortType sort,
    bool local_only,
    bool subscribed_only,
    Login login,
    PageCursor from
  ) -> PageOf<BoardDetail> {
    PageOf<BoardDetail> out { {}, !from, {} };
    optional<DBIter> iter;
    switch (sort) {
      case BoardSortType::New:
        iter.emplace(txn.list_boards_new(from.next_cursor_desc()));
        break;
      case BoardSortType::Old:
        iter.emplace(txn.list_boards_old(from.next_cursor_asc()));
        break;
      case BoardSortType::NewPosts:
        iter.emplace(txn.list_boards_new_posts(from.next_cursor_desc()));
        break;
      case BoardSortType::MostPosts:
        iter.emplace(txn.list_boards_most_posts(from.next_cursor_desc()));
        break;
      case BoardSortType::MostSubscribers:
        iter.emplace(txn.list_boards_most_subscribers(from.next_cursor_desc()));
        break;
    }
    assert(!!iter);
    for (const auto id : *iter) {
      if (subscribed_only && !(login && txn.is_user_subscribed_to_board(login->id, id))) continue;
      try {
        const auto d = BoardDetail::get(txn, id, login);
        if (local_only && d.board().instance()) continue;
        if (!d.should_show(login)) continue;
        out.entries.push_back(d);
      } catch (const ApiError& e) {
        spdlog::warn("Board {:x} error: {}", id, e.what());
      }
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) {
      out.next = PageCursor(iter->get_cursor()->int_field_0(), **iter);
    }
    return out;
  }
  static inline auto earliest_time(SortType sort) -> chrono::time_point<chrono::system_clock> {
    using namespace chrono;
    const auto now = system_clock::now();
    switch (sort) {
      case SortType::TopYear: return now - 24h * 365;
      case SortType::TopSixMonths: return now - 24h * 30 * 6;
      case SortType::TopThreeMonths: return now - 24h * 30 * 3;
      case SortType::TopMonth: return now - 24h * 30;
      case SortType::TopWeek: return now - 24h * 7;
      case SortType::TopDay: return now - 24h;
      case SortType::TopTwelveHour: return now - 12h;
      case SortType::TopSixHour: return now - 6h;
      case SortType::TopHour: return now - 1h;
      default: return time_point<system_clock>::min();
    }
  }
  static inline auto new_comments_cursor(PageCursor& from, optional<uint64_t> first_k = {}) -> optional<pair<Cursor, uint64_t>> {
    using namespace chrono;
    if (!from) return {};
    const auto time = (uint64_t)duration_cast<seconds>(
      (system_clock::time_point(seconds(from.k)) - ACTIVE_COMMENT_MAX_AGE).time_since_epoch()
    ).count();
    return pair(first_k ? Cursor(*first_k, time) : Cursor(time), from.v);
  }
  auto InstanceController::list_board_threads(
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    PageCursor from
  ) -> PageOf<ThreadDetail> {
    using namespace chrono;
    PageOf<ThreadDetail> out { {}, !from, {} };
    const auto board = txn.get_board(board_id);
    if (!board) throw ApiError("Board does not exist", 404);
    const auto get_entry = [&](uint64_t id) -> optional<ThreadDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(ThreadDetail::get(txn, id, login, {}, false, board, false));
      return e->should_show(login) ? e : nullopt;
    };
    optional<DBIter> iter;
    switch (sort) {
      case SortType::Active:
        out.next = ranked_active<ThreadDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_threads_of_board_new(board_id),
          txn.list_threads_of_board_top(board_id),
          get_entry,
          from.rank_k()
        );
        goto done;
      case SortType::Hot:
        out.next = ranked_hot<ThreadDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_threads_of_board_new(board_id),
          txn.list_threads_of_board_top(board_id),
          get_entry,
          from.rank_k()
        );
        goto done;
      case SortType::NewComments:
        out.next = ranked_new_comments<ThreadDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_threads_of_board_new(board_id, new_comments_cursor(from, board_id)),
          get_entry,
          from ? optional(system_clock::time_point(seconds(from.k))) : nullopt
        );
        goto done;
      case SortType::New:
        iter.emplace(txn.list_threads_of_board_new(board_id, from.next_cursor_desc(board_id)));
        break;
      case SortType::Old:
        iter.emplace(txn.list_threads_of_board_old(board_id, from.next_cursor_asc(board_id)));
        break;
      case SortType::MostComments:
        iter.emplace(txn.list_threads_of_board_most_comments(board_id, from.next_cursor_desc(board_id)));
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
        iter.emplace(txn.list_threads_of_board_top(board_id, from.next_cursor_desc(board_id)));
        break;
    }
    {
      assert(!!iter);
      const auto earliest = earliest_time(sort);
      for (uint64_t thread_id : *iter) {
        try {
          const auto entry = ThreadDetail::get(txn, thread_id, login, {}, false, board, false);
          const system_clock::time_point time(seconds(entry.thread().created_at()));
          if (time < earliest || !entry.should_show(login)) continue;
          out.entries.push_back(entry);
        } catch (const ApiError& e) {
          spdlog::warn("Thread {:x} error: {}", thread_id, e.what());
        }
        if (out.entries.full()) break;
      }
      if (!iter->is_done()) {
        out.next = PageCursor(iter->get_cursor()->int_field_1(), **iter);
      }
    }
  done:
    for (const auto& thread : out.entries) {
      if (thread.should_fetch_card()) event_bus->dispatch(Event::ThreadFetchLinkCard, thread.id);
    }
    return out;
  }
  auto InstanceController::list_board_comments(
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    PageCursor from
  ) -> PageOf<CommentDetail> {
    using namespace chrono;
    PageOf<CommentDetail> out { {}, !from, {} };
    const auto board = txn.get_board(board_id);
    if (!board) throw ApiError("Board does not exist", 404);
    const auto get_entry = [&](uint64_t id) -> optional<CommentDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(CommentDetail::get(txn, id, login, {}, false, {}, false, board, false));
      return e->should_show(login) ? e : nullopt;
    };
    optional<DBIter> iter;
    switch (sort) {
      case SortType::Active:
        out.next = ranked_active<CommentDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_comments_of_board_new(board_id),
          txn.list_comments_of_board_top(board_id),
          get_entry,
          from.rank_k()
        );
        return out;
      case SortType::Hot:
        out.next = ranked_hot<CommentDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_comments_of_board_new(board_id),
          txn.list_comments_of_board_top(board_id),
          get_entry,
          from.rank_k()
        );
        return out;
      case SortType::NewComments:
        out.next = ranked_new_comments<CommentDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_comments_of_board_new(board_id, new_comments_cursor(from, board_id)),
          get_entry,
          from ? optional(system_clock::time_point(seconds(from.k))) : nullopt
        );
        return out;
      case SortType::New:
        iter.emplace(txn.list_comments_of_board_new(board_id, from.next_cursor_desc(board_id)));
        break;
      case SortType::Old:
        iter.emplace(txn.list_comments_of_board_old(board_id, from.next_cursor_asc(board_id)));
        break;
      case SortType::MostComments:
        iter.emplace(txn.list_comments_of_board_most_comments(board_id, from.next_cursor_desc(board_id)));
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
        iter.emplace(txn.list_comments_of_board_top(board_id, from.next_cursor_desc(board_id)));
        break;
    }
    assert(!!iter);
    const auto earliest = earliest_time(sort);
    for (uint64_t comment_id : *iter) {
      try {
        const auto entry = CommentDetail::get(txn, comment_id, login, {}, false, {}, false, board, false);
        const system_clock::time_point time(seconds(entry.comment().created_at()));
        if (time < earliest || !entry.should_show(login)) continue;
        out.entries.push_back(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Comment {:x} error: {}", comment_id, e.what());
      }
      if (out.entries.full()) break;
    }
    if (!iter->is_done()) {
      out.next = PageCursor(iter->get_cursor()->int_field_1(), **iter);
    }
    return out;
  }
  auto InstanceController::list_feed_threads(
    ReadTxnBase& txn,
    uint64_t feed_id,
    SortType sort,
    Login login,
    PageCursor from
  ) -> PageOf<ThreadDetail> {
    using namespace chrono;
    PageOf<ThreadDetail> out { {}, !from, {} };
    function<bool (const ThreadDetail&)> filter_thread;
    switch (feed_id) {
      case FEED_ALL:
        filter_thread = [&](auto& e) { return e.should_show(login); };
        break;
      case FEED_LOCAL:
        filter_thread = [&](auto& e) { return !e.board().instance() && e.should_show(login); };
        break;
      case FEED_HOME: {
        if (!login) throw ApiError("Must be logged in to view Home feed", 403);
        std::set<uint64_t> subs;
        for (const auto id : txn.list_subscribed_boards(login->id)) subs.insert(id);
        filter_thread = [subs = std::move(subs), &login](auto& e) {
          return subs.contains(e.thread().board()) && e.should_show(login);
        };
        break;
      }
      default:
        throw ApiError(fmt::format("No feed with ID {:x}", feed_id), 404);
    }
    const auto get_entry = [&](uint64_t id) -> optional<ThreadDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(ThreadDetail::get(txn, id, login));
      return filter_thread(*e) ? e : nullopt;
    };
    optional<DBIter> iter;
    switch (sort) {
      case SortType::Active:
        out.next = ranked_active<ThreadDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_threads_new(),
          txn.list_threads_top(),
          get_entry,
          from.rank_k()
        );
        goto done;
      case SortType::Hot:
        out.next = ranked_hot<ThreadDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_threads_new(),
          txn.list_threads_top(),
          get_entry,
          from.rank_k()
        );
        goto done;
      case SortType::NewComments:
        out.next = ranked_new_comments<ThreadDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_threads_new(new_comments_cursor(from)),
          get_entry,
          from ? optional(system_clock::time_point(seconds(from.k))) : nullopt
        );
        goto done;
      case SortType::New:
        iter.emplace(txn.list_threads_new(from.next_cursor_desc()));
        break;
      case SortType::Old:
        iter.emplace(txn.list_threads_old(from.next_cursor_asc()));
        break;
      case SortType::MostComments:
        iter.emplace(txn.list_threads_most_comments(from.next_cursor_desc()));
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
        iter.emplace(txn.list_threads_top(from.next_cursor_desc()));
        break;
    }
    {
      assert(!!iter);
      const auto earliest = earliest_time(sort);
      for (uint64_t thread_id : *iter) {
        try {
          const auto entry = ThreadDetail::get(txn, thread_id, login);
          const system_clock::time_point time(seconds(entry.thread().created_at()));
          if (time < earliest || !entry.should_show(login)) continue;
          out.entries.push_back(entry);
        } catch (const ApiError& e) {
          spdlog::warn("Thread {:x} error: {}", thread_id, e.what());
        }
        if (out.entries.full()) break;
      }
      if (!iter->is_done()) {
        out.next = PageCursor(iter->get_cursor()->int_field_0(), **iter);
      }
    }
  done:
    for (const auto& thread : out.entries) {
      if (thread.should_fetch_card()) event_bus->dispatch(Event::ThreadFetchLinkCard, thread.id);
    }
    return out;
  }
  auto InstanceController::list_feed_comments(
    ReadTxnBase& txn,
    uint64_t feed_id,
    SortType sort,
    Login login,
    PageCursor from
  ) -> PageOf<CommentDetail> {
    using namespace chrono;
    PageOf<CommentDetail> out { {}, !from, {} };
    function<bool (const CommentDetail&)> filter_comment;
    switch (feed_id) {
      case FEED_ALL:
        filter_comment = [&](auto& e) { return e.should_show(login); };
        break;
      case FEED_LOCAL:
        filter_comment = [&](auto& e) { return !e.board().instance() && e.should_show(login); };
        break;
      case FEED_HOME: {
        if (!login) throw ApiError("Must be logged in to view Home feed", 403);
        std::set<uint64_t> subs;
        for (const auto id : txn.list_subscribed_boards(login->id)) subs.insert(id);
        filter_comment = [subs = std::move(subs), &login](auto& e) {
          return subs.contains(e.thread().board()) && e.should_show(login);
        };
        break;
      }
      default:
        throw ApiError(fmt::format("No feed with ID {:x}", feed_id), 404);
    }
    const auto get_entry = [&](uint64_t id) -> optional<CommentDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(CommentDetail::get(txn, id, login));
      return filter_comment(*e) ? e : nullopt;
    };
    optional<DBIter> iter;
    switch (sort) {
      case SortType::Active:
        out.next = ranked_active<CommentDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_comments_new(),
          txn.list_comments_top(),
          get_entry,
          from.rank_k()
        );
        return out;
      case SortType::Hot:
        out.next = ranked_hot<CommentDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_comments_new(),
          txn.list_comments_top(),
          get_entry,
          from.rank_k()
        );
        return out;
      case SortType::NewComments:
        out.next = ranked_new_comments<CommentDetail, ITEMS_PER_PAGE>(
          out.entries,
          txn,
          txn.list_comments_new(new_comments_cursor(from)),
          get_entry,
          from ? optional(system_clock::time_point(seconds(from.k))) : nullopt
        );
        return out;
      case SortType::New:
        iter.emplace(txn.list_comments_new(from.next_cursor_desc()));
        break;
      case SortType::Old:
        iter.emplace(txn.list_comments_old(from.next_cursor_asc()));
        break;
      case SortType::MostComments:
        iter.emplace(txn.list_comments_most_comments(from.next_cursor_desc()));
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
        iter.emplace(txn.list_comments_top(from.next_cursor_desc()));
        break;
    }
    assert(!!iter);
    const auto earliest = earliest_time(sort);
    for (uint64_t comment_id : *iter) {
      try {
        const auto entry = CommentDetail::get(txn, comment_id, login);
        const system_clock::time_point time(seconds(entry.comment().created_at()));
        if (time < earliest || !entry.should_show(login)) continue;
        out.entries.push_back(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Comment {:x} error: {}", comment_id, e.what());
      }
      if (out.entries.full()) break;
    }
    if (!iter->is_done()) {
      out.next = PageCursor(iter->get_cursor()->int_field_0(), **iter);
    }
    return out;
  }
  auto InstanceController::list_user_threads(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    PageCursor from
  ) -> PageOf<ThreadDetail> {
    PageOf<ThreadDetail> out { {}, !from, {} };
    const auto user = txn.get_user(user_id);
    if (!user) throw ApiError("User does not exist", 404);
    optional<DBIter> iter;
    switch (sort) {
      case UserPostSortType::New:
        iter.emplace(txn.list_threads_of_user_new(user_id, from.next_cursor_desc(user_id)));
        break;
      case UserPostSortType::Old:
        iter.emplace(txn.list_threads_of_user_old(user_id, from.next_cursor_asc(user_id)));
        break;
      case UserPostSortType::Top:
        iter.emplace(txn.list_threads_of_user_top(user_id, from.next_cursor_desc(user_id)));
        break;
    }
    assert(!!iter);
    for (uint64_t thread_id : *iter) {
      try {
        const auto entry = ThreadDetail::get(txn, thread_id, login, user);
        if (!entry.should_show(login)) continue;
        out.entries.push_back(std::move(entry));
      } catch (const ApiError& e) {
        spdlog::warn("Thread {:x} error: {}", thread_id, e.what());
      }
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) {
      out.next = PageCursor(iter->get_cursor()->int_field_1(), **iter);
    }
    for (const auto& thread : out.entries) {
      if (thread.should_fetch_card()) {
        event_bus->dispatch(Event::ThreadFetchLinkCard, thread.id);
      }
    }
    return out;
  }
  auto InstanceController::list_user_comments(
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    PageCursor from
  ) -> PageOf<CommentDetail> {
    PageOf<CommentDetail> out { {}, !from, {} };
    optional<DBIter> iter;
    switch (sort) {
      case UserPostSortType::New:
        iter.emplace(txn.list_comments_of_user_new(user_id, from.next_cursor_desc(user_id)));
        break;
      case UserPostSortType::Old:
        iter.emplace(txn.list_comments_of_user_old(user_id, from.next_cursor_asc(user_id)));
        break;
      case UserPostSortType::Top:
        iter.emplace(txn.list_comments_of_user_top(user_id, from.next_cursor_desc(user_id)));
        break;
    }
    assert(!!iter);
    for (uint64_t comment_id : *iter) {
      try {
        const auto entry = CommentDetail::get(txn, comment_id, login);
        if (!entry.should_show(login)) continue;
        out.entries.push_back(std::move(entry));
      } catch (const ApiError& e) {
        spdlog::warn("Comment {:x} error: {}", comment_id, e.what());
      }
      if (out.entries.size() >= ITEMS_PER_PAGE) break;
    }
    if (!iter->is_done()) {
      out.next = PageCursor(iter->get_cursor()->int_field_1(), **iter);
    }
    return out;
  }
  auto InstanceController::search_step_1(SearchQuery query, SearchEngine::Callback&& callback) -> void {
    if (!search_engine) throw ApiError("Search is not enabled on this server", 403);
    query.limit = (query.limit ? query.limit : ITEMS_PER_PAGE) * 2;
    (*search_engine)->search(query, std::forward<SearchEngine::Callback>(callback));
  }
  auto InstanceController::search_step_2(
    ReadTxnBase& txn,
    const vector<SearchResult>& results,
    size_t max_len,
    Login login
  ) -> vector<SearchResultDetail> {
    vector<SearchResultDetail> out;
    out.reserve(min(results.size(), max_len));
    for (const auto& result : results) {
      const auto id = result.id;
      try {
        switch (result.type) {
        case SearchResultType::User: {
          const auto entry = UserDetail::get(txn, id, login);
          if (entry.should_show(login)) out.emplace_back(entry);
          break;
        }
        case SearchResultType::Board: {
          const auto entry = BoardDetail::get(txn, id, login);
          if (entry.should_show(login)) out.emplace_back(entry);
          break;
        }
        case SearchResultType::Thread: {
          const auto entry = ThreadDetail::get(txn, id, login);
          if (entry.should_show(login)) out.emplace_back(entry);
          break;
        }
        case SearchResultType::Comment: {
          const auto entry = CommentDetail::get(txn, id, login);
          if (entry.should_show(login)) out.emplace_back(entry);
          break;
        }
        }
      } catch (const ApiError& e) {
        spdlog::warn("Search result {:x} error: {}", id, e.what());
      }
      if (out.size() >= max_len) break;
    }
    return out;
  }

  auto InstanceController::update_site(const SiteUpdate& update) -> void {
    {
      auto txn = db->open_write_txn();
      if (const auto v = update.name) txn.set_setting(SettingsKey::name, *v);
      if (const auto v = update.description) txn.set_setting(SettingsKey::description, *v);
      if (const auto v = update.icon_url) txn.set_setting(SettingsKey::icon_url, v->value_or(""));
      if (const auto v = update.banner_url) txn.set_setting(SettingsKey::banner_url, v->value_or(""));
      if (const auto v = update.max_post_length) txn.set_setting(SettingsKey::post_max_length, *v);
      if (const auto v = update.javascript_enabled) txn.set_setting(SettingsKey::javascript_enabled, *v);
      if (const auto v = update.board_creation_admin_only) txn.set_setting(SettingsKey::board_creation_admin_only, *v);
      if (const auto v = update.registration_enabled) txn.set_setting(SettingsKey::registration_enabled, *v);
      if (const auto v = update.registration_application_required) txn.set_setting(SettingsKey::registration_application_required, *v);
      if (const auto v = update.registration_invite_required) txn.set_setting(SettingsKey::registration_invite_required, *v);
      if (const auto v = update.invite_admin_only) txn.set_setting(SettingsKey::invite_admin_only, *v);
      txn.commit();
    }
    {
      auto txn = db->open_read_txn();
      auto new_detail = new SiteDetail;
      *new_detail = SiteDetail::get(txn);
      auto old_detail = cached_site_detail.exchange(new_detail);
      if (old_detail) delete old_detail;
    }
    event_bus->dispatch(Event::SiteUpdate);
  }

  auto InstanceController::create_local_user_internal(
    WriteTxn& txn,
    string_view username,
    optional<string_view> email,
    SecretString&& password,
    bool is_approved,
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
    FlatBufferBuilder fbb;
    {
      const auto name_s = fbb.CreateString(username);
      UserBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_name(name_s);
      b.add_bot(is_bot);
      fbb.Finish(b.Finish());
    }
    const auto user_id = txn.create_user(fbb.GetBufferSpan());
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
      b.add_approved(is_approved);
      if (invite) b.add_invite(*invite);
      fbb.Finish(b.Finish());
    }
    txn.set_local_user(user_id, fbb.GetBufferSpan());
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
    const bool approved = !site->registration_application_required;
    const auto user_id = create_local_user_internal(
      txn, username, email, std::move(password), approved, false, invite_id
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
      txn.set_invite(*invite_id, fbb.GetBufferSpan());
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
      txn.create_application(user_id, fbb.GetBufferSpan());
    }
    txn.commit();
    return { user_id, approved };
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
      txn, username, email, std::move(password), true, is_bot, invite
    );
    txn.commit();
    return user_id;
  }
  auto InstanceController::update_local_user(uint64_t id, const LocalUserUpdate& update) -> void {
    auto txn = db->open_write_txn();
    const auto detail = LocalUserDetail::get(txn, id);
    if (update.email && !regex_match(*update.email, email_regex)) {
      throw ApiError("Invalid email address", 400);
    }
    if (update.email && txn.get_user_id_by_email(string(*update.email))) {
      throw ApiError("A user with this email address already exists on this instance", 409);
    }
    if (update.display_name && *update.display_name && (*update.display_name)->length() > 1024) {
      throw ApiError("Display name cannot be longer than 1024 bytes", 400);
    }
    if (update.email || update.approved || update.accepted_application ||
        update.email_verified || update.open_links_in_new_tab ||
        update.show_avatars || update.show_bot_accounts ||
        update.hide_cw_posts || update.expand_cw_posts ||
        update.expand_cw_images || update.show_karma ||
        update.javascript_enabled) {
      const auto& lu = detail.local_user();
      FlatBufferBuilder fbb;
      fbb.Finish(CreateLocalUserDirect(fbb,
        update.email.value_or(lu.email()->c_str()),
        lu.password_hash(),
        lu.password_salt(),
        lu.admin(),
        update.approved.value_or(lu.approved()),
        update.accepted_application.value_or(lu.accepted_application()),
        update.email_verified.value_or(lu.email_verified()),
        lu.invite(),
        update.open_links_in_new_tab.value_or(lu.open_links_in_new_tab()),
        lu.send_notifications_to_email(),
        update.show_avatars.value_or(lu.show_avatars()),
        lu.show_images_threads(),
        lu.show_images_comments(),
        update.show_bot_accounts.value_or(lu.show_bot_accounts()),
        lu.show_new_post_notifs(),
        update.hide_cw_posts.value_or(lu.hide_cw_posts()),
        update.expand_cw_posts.value_or(lu.expand_cw_posts()),
        update.expand_cw_images.value_or(lu.expand_cw_images()),
        lu.show_read_posts(),
        update.show_karma.value_or(lu.show_karma()),
        update.javascript_enabled.value_or(lu.javascript_enabled()),
        lu.infinite_scroll_enabled(),
        lu.interface_language() ? lu.interface_language()->c_str() : nullptr,
        lu.theme() ? lu.theme()->c_str() : nullptr,
        lu.default_sort_type(),
        lu.default_comment_sort_type()
      ));
      txn.set_local_user(id, fbb.GetBufferSpan());
    }
    if (update.display_name || update.bio || update.avatar_url || update.banner_url) {
      const auto& u = detail.user();
      FlatBufferBuilder fbb;
      const auto [display_name_type, display_name] =
        update.display_name
          .transform([](optional<string_view> sv) { return sv.transform(λx(string(x))); })
          .value_or(u.display_name_type()->size()
            ? optional(rich_text->plain_text_with_emojis_to_text_content(u.display_name_type(), u.display_name()))
            : nullopt)
          .transform([&](string s) { return rich_text->parse_plain_text_with_emojis(fbb, s); })
          .value_or(pair(0, 0));
      const auto [bio_raw, bio_type, bio] =
        update.bio
          .value_or(u.bio_raw() ? optional(u.bio_raw()->string_view()) : nullopt)
          .transform([&](string_view s) {
            const auto [bio_type, bio] = rich_text->parse_markdown(fbb, s);
            return tuple(fbb.CreateString(s), bio_type, bio);
          })
          .value_or(tuple(0, 0, 0));
      fbb.Finish(CreateUser(fbb,
        fbb.CreateString(u.name()),
        display_name_type, display_name,
        bio_raw, bio_type, bio,
        0, 0, {},
        u.created_at(), now_s(), u.deleted_at(),
        update.avatar_url.value_or(u.avatar_url() ? optional(u.avatar_url()->string_view()) : nullopt)
          .transform([&](auto s) { return fbb.CreateString(s); }).value_or(0),
        update.banner_url.value_or(u.banner_url() ? optional(u.banner_url()->string_view()) : nullopt)
          .transform([&](auto s) { return fbb.CreateString(s); }).value_or(0),
        u.bot(),
        u.mod_state(), fbb.CreateString(u.mod_reason())
      ));
      txn.set_user(id, fbb.GetBufferSpan());
    }
    txn.commit();
  }
  auto InstanceController::approve_local_user_application(uint64_t user_id) -> void {
    LocalUserUpdate update;
    {
      auto txn = db->open_read_txn();
      const auto old_opt = txn.get_local_user(user_id);
      if (!old_opt) throw ApiError("User does not exist", 404);
      const auto& old = old_opt->get();
      if (old.accepted_application()) throw ApiError("User's application has already been accepted", 409);
      if (!txn.get_application(user_id)) throw ApiError("User does not have an application to approve", 404);
      update.accepted_application = true;
      update.approved = old.approved() || site_detail()->registration_application_required;
    }
    update_local_user(user_id, update);
  }
  auto InstanceController::create_site_invite(uint64_t inviter_user_id) -> uint64_t {
    auto txn = db->open_write_txn();
    const auto user = LocalUserDetail::get(txn, inviter_user_id);
    if (site_detail()->invite_admin_only && !user.local_user().admin()) {
      throw ApiError("Only admins can create invite codes", 403);
    }
    const auto id = txn.create_invite(inviter_user_id, 86400 * 7);
    txn.commit();
    return id;
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
    if (!can_create_board(LocalUserDetail::get(txn, owner))) {
      throw ApiError("User does not have permission to create boards", 403);
    }
    FlatBufferBuilder fbb;
    {
      Offset<Vector<PlainTextWithEmojis>> display_name_types;
      Offset<Vector<Offset<void>>> display_name_values;
      if (display_name) {
        const auto display_name_s = fbb.CreateString(*display_name);
        display_name_types = fbb.CreateVector<PlainTextWithEmojis>(vector{PlainTextWithEmojis::Plain});
        display_name_values = fbb.CreateVector<Offset<void>>(vector{display_name_s.Union()});
      }
      const auto content_warning_s = content_warning.transform([&](auto s) { return fbb.CreateString(s); });
      const auto name_s = fbb.CreateString(name);
      BoardBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_name(name_s);
      if (display_name) {
        b.add_display_name_type(display_name_types);
        b.add_display_name(display_name_values);
      }
      if (content_warning) b.add_content_warning(*content_warning_s);
      b.add_restricted_posting(is_restricted_posting);
      fbb.Finish(b.Finish());
    }
    const auto board_id = txn.create_board(fbb.GetBufferSpan());
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
    txn.set_local_board(board_id, fbb.GetBufferSpan());
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
      optional<LocalUserDetail> user;
      try { user = LocalUserDetail::get(txn, author); }
      catch (const ApiError&) { throw ApiError("User does not exist", 403); }
      if (!BoardDetail::get(txn, board, user).can_create_thread(user)) {
        throw ApiError("User cannot create a thread in this board", 403);
      }
      FlatBufferBuilder fbb;
      const auto title_s = fbb.CreateString(title),
        submission_s = submission_url ? fbb.CreateString(*submission_url) : 0,
        content_raw_s = text_content_markdown ? fbb.CreateString(*text_content_markdown) : 0,
        content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0;
      pair<Offset<Vector<TextBlock>>, Offset<Vector<Offset<void>>>> content_blocks;
      if (text_content_markdown) content_blocks = rich_text->parse_markdown(fbb, *text_content_markdown);
      ThreadBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_author(author);
      b.add_board(board);
      b.add_title(title_s);
      if (submission_url) b.add_content_url(submission_s);
      if (text_content_markdown) {
        b.add_content_text_raw(content_raw_s);
        b.add_content_text_type(content_blocks.first);
        b.add_content_text(content_blocks.second);
      }
      if (content_warning) b.add_content_warning(content_warning_s);
      fbb.Finish(b.Finish());
      thread_id = txn.create_thread(fbb.GetBufferSpan());
      if (search_engine) {
        (*search_engine)->index(thread_id, *GetRoot<Thread>(fbb.GetBufferPointer()));
      }
      txn.set_vote(author, thread_id, Vote::Upvote);
      txn.commit();
    }
    event_bus->dispatch(Event::UserStatsUpdate, author);
    event_bus->dispatch(Event::BoardStatsUpdate, board);
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
    auto txn = db->open_write_txn();
    const auto login = LocalUserDetail::get(txn, author);
    optional<ThreadDetail> parent_thread;
    optional<CommentDetail> parent_comment;
    try {
      parent_thread = ThreadDetail::get(txn, parent, login);
    } catch (...) {
      parent_comment = CommentDetail::get(txn, parent, login);
      parent_thread = ThreadDetail::get(txn, parent_comment->comment().thread(), login);
    }
    if (parent_comment ? !parent_comment->can_reply_to(login) : !parent_thread->can_reply_to(login)) {
      throw ApiError("User cannot reply to this post", 403);
    }
    FlatBufferBuilder fbb;
    const auto content_raw_s = fbb.CreateString(text_content_markdown),
      content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0;
    const auto content_blocks = rich_text->parse_markdown(fbb, text_content_markdown);
    CommentBuilder b(fbb);
    b.add_created_at(now_s());
    b.add_author(author);
    b.add_thread(parent_thread->id);
    b.add_parent(parent);
    b.add_content_raw(content_raw_s);
    b.add_content_type(content_blocks.first);
    b.add_content(content_blocks.second);
    if (content_warning) b.add_content_warning(content_warning_s);
    fbb.Finish(b.Finish());
    const auto comment_id = txn.create_comment(fbb.GetBufferSpan());
    const auto board_id = parent_thread->thread().board();
    if (search_engine) {
      (*search_engine)->index(comment_id, *GetRoot<Comment>(fbb.GetBufferPointer()));
    }
    txn.set_vote(author, comment_id, Vote::Upvote);
    txn.commit();
    event_bus->dispatch(Event::UserStatsUpdate, author);
    event_bus->dispatch(Event::BoardStatsUpdate, board_id);
    event_bus->dispatch(Event::PostStatsUpdate, parent_thread->id);
    if (parent != parent_thread->id) event_bus->dispatch(Event::PostStatsUpdate, parent);
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

    event_bus->dispatch(Event::UserStatsUpdate, op);
    event_bus->dispatch(Event::PostStatsUpdate, post_id);
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

    event_bus->dispatch(Event::UserStatsUpdate, user_id);
    event_bus->dispatch(Event::BoardStatsUpdate, board_id);
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
