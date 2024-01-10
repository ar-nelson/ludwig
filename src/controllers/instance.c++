#include "instance.h++"
#include <regex>
#include <queue>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <static_vector.hpp>
#include "util/web.h++"
#include "models/patch.h++"

using std::function, std::max, std::min, std::nullopt, std::optional, std::pair,
    std::priority_queue, std::regex, std::regex_match, std::shared_ptr,
    std::string, std::string_view, std::unique_ptr, std::vector, flatbuffers::Offset,
    flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot,
    flatbuffers::Vector;
namespace chrono = std::chrono;

#define SECONDS(N) chrono::system_clock::time_point(chrono::seconds(N))

namespace Ludwig {
  // PBKDF2-HMAC-SHA256 iteration count, as suggested by
  // https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html#pbkdf2
  static constexpr uint32_t PASSWORD_HASH_ITERATIONS = 600'000;

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

  template <class T>
  static inline auto finish_ranked_queue(
    Writer<T> out,
    RankedQueue& queue,
    function<optional<T> (uint64_t)>& get_entry,
    uint16_t limit,
    double last_rank
  ) -> PageCursor {
    while (!queue.empty()) {
      const auto [id, rank] = queue.top();
      queue.pop();
      if (auto entry = get_entry(id)) {
        if (limit == 0) return PageCursor(last_rank, id);
        entry->rank = last_rank = rank;
        out(*entry);
        limit--;
      }
    }
    return {};
  }

  template <class T>
  static inline auto ranked_active(
    Writer<T> out,
    ReadTxnBase& txn,
    DBIter iter_by_new,
    DBIter iter_by_top,
    function<optional<T> (uint64_t)> get_entry,
    double max_rank = INFINITY,
    uint16_t limit = ITEMS_PER_PAGE
  ) -> PageCursor {
    int64_t max_possible_karma;
    if (limit == 0 || iter_by_top.is_done() || iter_by_new.is_done()) return {};
    if (auto top_stats = txn.get_post_stats(*iter_by_top)) {
      max_possible_karma = top_stats->get().karma();
    } else return {};
    const auto max_possible_numerator = rank_numerator(max_possible_karma);
    const auto now = chrono::system_clock::now();
    double last_rank;
    RankedQueue queue(ranked_id_cmp);
    for (auto id : iter_by_new) {
      const auto stats = txn.get_post_stats(id);
      if (!stats) continue;
      const auto timestamp = SECONDS(stats->get().latest_comment());
      const auto denominator = rank_denominator(now - timestamp);
      const auto rank = rank_numerator(stats->get().karma()) / denominator;
      if (rank >= max_rank) continue;
      queue.emplace(id, rank);
      const auto latest_possible_timestamp = min(now, T::get_created_at(txn, id) + ACTIVE_COMMENT_MAX_AGE);
      const auto [top_id, top_rank] = queue.top();
      const double
        min_possible_denominator =
          rank_denominator(latest_possible_timestamp < now ? now - latest_possible_timestamp : chrono::seconds::zero()),
        max_possible_rank = max_possible_numerator / min_possible_denominator;
      if (max_possible_rank > top_rank) continue;
      queue.pop();
      if (auto entry = get_entry(top_id)) {
        entry->rank = last_rank = top_rank;
        out(*entry);
        if (--limit == 0) break;
      }
    }
    return finish_ranked_queue(out, queue, get_entry, limit, last_rank);
  }

  template <class T>
  static inline auto ranked_hot(
    Writer<T> out,
    ReadTxnBase& txn,
    DBIter iter_by_new,
    DBIter iter_by_top,
    function<optional<T> (uint64_t)> get_entry,
    double max_rank = INFINITY,
    uint16_t limit = ITEMS_PER_PAGE
  ) -> PageCursor {
    int64_t max_possible_karma;
    if (limit == 0 || iter_by_top.is_done() || iter_by_new.is_done()) return {};
    if (auto top_stats = txn.get_post_stats(*iter_by_top)) {
      max_possible_karma = top_stats->get().karma();
    } else return {};
    const auto max_possible_numerator = rank_numerator(max_possible_karma);
    const auto now = chrono::system_clock::now();
    double last_rank;
    RankedQueue queue(ranked_id_cmp);
    for (auto id : iter_by_new) {
      const auto timestamp = T::get_created_at(txn, id);
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
        entry->rank = last_rank = top_rank;
        out(*entry);
        if (--limit == 0) break;
      }
    }
    return finish_ranked_queue(out, queue, get_entry, limit, last_rank);
  }

  template <class T>
  static inline auto ranked_new_comments(
    Writer<T> out,
    ReadTxnBase& txn,
    DBIter iter_by_new,
    function<optional<T> (uint64_t)> get_entry,
    optional<chrono::system_clock::time_point> from = {},
    uint16_t limit = ITEMS_PER_PAGE
  ) -> PageCursor {
    using IdTime = pair<uint64_t, chrono::system_clock::time_point>;
    static constexpr auto id_time_cmp = [](const IdTime& a, const IdTime& b) -> bool { return a.second < b.second; };
    const auto now = chrono::system_clock::now();
    const auto max_time = from.value_or(now);
    uint64_t last_time;
    priority_queue<IdTime, vector<IdTime>, decltype(id_time_cmp)> queue;
    for (uint64_t id : iter_by_new) {
      const auto stats = txn.get_post_stats(id);
      if (!stats) continue;
      const auto timestamp = SECONDS(stats->get().latest_comment());
      if (timestamp >= max_time) continue;
      queue.emplace(id, timestamp);
      const auto [top_id, top_time] = queue.top();
      const auto max_possible_time = min(now, T::get_created_at(txn, id) + ACTIVE_COMMENT_MAX_AGE);
      if (max_possible_time > top_time) continue;
      queue.pop();
      if (auto entry = get_entry(top_id)) {
        last_time = stats->get().latest_comment();
        out(*entry);
        if (--limit == 0) break;
      }
    }
    while (!queue.empty()) {
      const auto [id, timestamp] = queue.top();
      queue.pop();
      if (auto entry = get_entry(id)) {
        if (limit == 0) return PageCursor(last_time);
        last_time = entry->stats().latest_comment();
        out(*entry);
        --limit;
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
    uint16_t max_comments = ITEMS_PER_PAGE,
    uint16_t max_depth = 5
  ) -> void {
    if (!max_depth) {
      tree.mark_continued(parent);
      return;
    }
    if (tree.size() >= max_comments) return;
    optional<DBIter> iter;
    switch (sort) {
      case CommentSortType::Hot: {
        vector<CommentDetail> entries;
        entries.reserve(max_comments);
        auto page_cursor = ranked_hot<CommentDetail>(
          [&](auto& x){entries.push_back(x);},
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
          from.rank_k(),
          max_comments
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
    optional<shared_ptr<SearchEngine>> search_engine,
    optional<pair<Hash, Salt>> first_run_admin_password
  ) : db(db),
      http_client(http_client),
      rich_text(rich_text),
      event_bus(event_bus),
      site_detail_sub(event_bus->on_event(Event::SiteUpdate, [&](Event, uint64_t){
        auto txn = open_read_txn();
        auto detail = new SiteDetail;
        *detail = SiteDetail::get(txn);
        auto ptr = cached_site_detail.exchange(detail);
        if (ptr) delete ptr;
      })),
      search_engine(search_engine),
      first_run_admin_password(first_run_admin_password) {
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
      password.data.data(), password.data.length(),
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

    // Don't allow logins as the temp admin user after setup is done
    if (!user && site_detail()->setup_done) {
      auto txn = db->open_write_txn();
      txn.delete_session(session_id);
      txn.commit();
      return {};
    }

    if (session.remember() && system_clock::now() - SECONDS(session.created_at()) >= hours(24)) {
      auto txn = db->open_write_txn();
      const auto [id, expiration] = txn.create_session(
        user,
        ip,
        user_agent,
        true,
        session.expires_at() - session.created_at()
      );
      txn.delete_session(session_id);
      txn.commit();
      return { { .user_id = user, .session_id = id, .expiration = SECONDS(expiration)} };
    }
    return { { .user_id = user, .session_id = session_id, .expiration = SECONDS(session.expires_at()) } };
  }
  auto InstanceController::login(
    string_view username_or_email,
    SecretString&& password,
    string_view ip,
    string_view user_agent,
    bool remember
  ) -> LoginResponse {
    using namespace chrono;
    uint8_t hash[32];
    const Hash* target_hash;
    const Salt* salt;
    uint64_t user_id = 0;

    auto txn = db->open_write_txn();
    const bool is_first_run_admin =
      first_run_admin_password &&
      !site_detail()->setup_done &&
      txn.get_admin_list().empty() &&
      username_or_email == FIRST_RUN_ADMIN_USERNAME;
    if (is_first_run_admin) {
      target_hash = &first_run_admin_password->first;
      salt = &first_run_admin_password->second;
    } else {
      const auto user_id_opt = username_or_email.find('@') == string_view::npos
        ? txn.get_user_id_by_name(username_or_email)
        : txn.get_user_id_by_email(username_or_email);
      if (!user_id_opt && !is_first_run_admin) {
        throw ApiError("Invalid username or password", 400,
          fmt::format("Tried to log in as nonexistent user {}", username_or_email)
        );
      }
      user_id = *user_id_opt;
      const auto local_user = txn.get_local_user(user_id);
      if (!local_user) {
        throw ApiError("Invalid username or password", 400,
          fmt::format("Tried to log in as non-local user {}", username_or_email)
        );
      }
      target_hash = local_user->get().password_hash();
      salt = local_user->get().password_salt();
    }

    hash_password(std::move(password), salt->bytes()->Data(), hash);

    // Note that this returns 0 on success, 1 on failure!
    if (CRYPTO_memcmp(hash, target_hash->bytes()->Data(), 32)) {
      // TODO: Lock users out after repeated failures
      throw ApiError("Invalid username or password", 400,
        fmt::format("Tried to login with wrong password for user {}", username_or_email)
      );
    }
    const auto [session_id, expiration] = txn.create_session(
      user_id,
      ip,
      user_agent,
      remember,
      remember ? duration_cast<seconds>(months{1}).count()
        : duration_cast<seconds>(days{1}).count()
    );
    txn.commit();
    return { .user_id = user_id, .session_id = session_id, .expiration = SECONDS(expiration) };
  }
  auto InstanceController::thread_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> pair<ThreadDetail, CommentTree> {
    pair<ThreadDetail, CommentTree> p(ThreadDetail::get(txn, id, login), {});
    if (!p.first.can_view(login)) throw ApiError("Cannot view this thread", 403);
    if (p.first.should_fetch_card()) event_bus->dispatch(Event::ThreadFetchLinkCard, id);
    comment_tree(
      txn, p.second, id, sort, login,
      p.first.thread(), p.first.hidden,
      p.first.board(), p.first.board_hidden,
      from, limit
    );
    return p;
  }
  auto InstanceController::comment_detail(
    ReadTxnBase& txn,
    uint64_t id,
    CommentSortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> pair<CommentDetail, CommentTree> {
    pair<CommentDetail, CommentTree> p(CommentDetail::get(txn, id, login), {});
    if (!p.first.can_view(login)) throw ApiError("Cannot view this comment", 403);
    comment_tree(
      txn, p.second, id, sort, login,
      p.first.thread(), p.first.thread_hidden,
      p.first.board(), p.first.board_hidden,
      from, limit
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
    Writer<UserDetail> out,
    ReadTxnBase& txn,
    UserSortType sort,
    bool local_only,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
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
        out(d);
      } catch (const ApiError& e) {
        spdlog::warn("User {:x} error: {}", id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_0(), **iter);
  }
  auto InstanceController::list_boards(
    Writer<BoardDetail> out,
    ReadTxnBase& txn,
    BoardSortType sort,
    bool local_only,
    bool subscribed_only,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
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
        out(d);
      } catch (const ApiError& e) {
        spdlog::warn("Board {:x} error: {}", id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_0(), **iter);
  }
  static inline auto earliest_time(SortType sort) -> chrono::system_clock::time_point {
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
      default: return system_clock::time_point::min();
    }
  }
  static inline auto new_comments_cursor(PageCursor& from, optional<uint64_t> first_k = {}) -> optional<pair<Cursor, uint64_t>> {
    using namespace chrono;
    if (!from) return {};
    const auto time = (uint64_t)duration_cast<seconds>(
      (SECONDS(from.k) - ACTIVE_COMMENT_MAX_AGE).time_since_epoch()
    ).count();
    return pair(first_k ? Cursor(*first_k, time) : Cursor(time), from.v);
  }
  auto InstanceController::list_board_threads(
    Writer<ThreadDetail> out,
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
    using namespace chrono;
    const auto board = txn.get_board(board_id);
    if (!board) throw ApiError("Board does not exist", 410);
    const auto out_with_card = [&](const ThreadDetail& thread) {
      if (thread.should_fetch_card()) event_bus->dispatch(Event::ThreadFetchLinkCard, thread.id);
      out(thread);
    };
    const auto get_entry = [&](uint64_t id) -> optional<ThreadDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(ThreadDetail::get(txn, id, login, {}, false, board, false));
      return e->should_show(login) ? e : nullopt;
    };
    optional<DBIter> iter;
    PageCursor next;
    switch (sort) {
      case SortType::Active:
        return ranked_active<ThreadDetail>(
          out_with_card,
          txn,
          txn.list_threads_of_board_new(board_id),
          txn.list_threads_of_board_top(board_id),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::Hot:
        return ranked_hot<ThreadDetail>(
          out_with_card,
          txn,
          txn.list_threads_of_board_new(board_id),
          txn.list_threads_of_board_top(board_id),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::NewComments:
        return ranked_new_comments<ThreadDetail>(
          out_with_card,
          txn,
          txn.list_threads_of_board_new(board_id, new_comments_cursor(from, board_id)),
          get_entry,
          from ? optional(SECONDS(from.k)) : nullopt,
          limit
        );
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
    assert(!!iter);
    const auto earliest = earliest_time(sort);
    for (uint64_t thread_id : *iter) {
      try {
        const auto entry = ThreadDetail::get(txn, thread_id, login, {}, false, board, false);
        const auto time = SECONDS(entry.thread().created_at());
        if (time < earliest || !entry.should_show(login)) continue;
        out_with_card(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Thread {:x} error: {}", thread_id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_1(), **iter);
  }
  auto InstanceController::list_board_comments(
    Writer<CommentDetail> out,
    ReadTxnBase& txn,
    uint64_t board_id,
    SortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
    const auto board = txn.get_board(board_id);
    if (!board) throw ApiError("Board does not exist", 410);
    const auto get_entry = [&](uint64_t id) -> optional<CommentDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(CommentDetail::get(txn, id, login, {}, false, {}, false, board, false));
      return e->should_show(login) ? e : nullopt;
    };
    optional<DBIter> iter;
    switch (sort) {
      case SortType::Active:
        return ranked_active<CommentDetail>(
          out,
          txn,
          txn.list_comments_of_board_new(board_id),
          txn.list_comments_of_board_top(board_id),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::Hot:
        return ranked_hot<CommentDetail>(
          out,
          txn,
          txn.list_comments_of_board_new(board_id),
          txn.list_comments_of_board_top(board_id),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::NewComments:
        return ranked_new_comments<CommentDetail>(
          out,
          txn,
          txn.list_comments_of_board_new(board_id, new_comments_cursor(from, board_id)),
          get_entry,
          from ? optional(SECONDS(from.k)) : nullopt,
          limit
        );
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
        const auto time = SECONDS(entry.comment().created_at());
        if (time < earliest || !entry.should_show(login)) continue;
        out(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Comment {:x} error: {}", comment_id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_1(), **iter);
  }
  auto InstanceController::list_feed_threads(
    Writer<ThreadDetail> out,
    ReadTxnBase& txn,
    uint64_t feed_id,
    SortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
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
        throw ApiError(fmt::format("No feed with ID {:x}", feed_id), 410);
    }
    const auto out_with_card = [&](const ThreadDetail& thread) {
      if (thread.should_fetch_card()) event_bus->dispatch(Event::ThreadFetchLinkCard, thread.id);
      out(thread);
    };
    const auto get_entry = [&](uint64_t id) -> optional<ThreadDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(ThreadDetail::get(txn, id, login));
      return filter_thread(*e) ? e : nullopt;
    };
    optional<DBIter> iter;
    switch (sort) {
      case SortType::Active:
        return ranked_active<ThreadDetail>(
          out_with_card,
          txn,
          txn.list_threads_new(),
          txn.list_threads_top(),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::Hot:
        return ranked_hot<ThreadDetail>(
          out_with_card,
          txn,
          txn.list_threads_new(),
          txn.list_threads_top(),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::NewComments:
        return ranked_new_comments<ThreadDetail>(
          out_with_card,
          txn,
          txn.list_threads_new(new_comments_cursor(from)),
          get_entry,
          from ? optional(SECONDS(from.k)) : nullopt,
          limit
        );
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
    assert(!!iter);
    const auto earliest = earliest_time(sort);
    for (uint64_t thread_id : *iter) {
      try {
        const auto entry = ThreadDetail::get(txn, thread_id, login);
        const auto time = SECONDS(entry.thread().created_at());
        if (time < earliest || !entry.should_show(login)) continue;
        out_with_card(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Thread {:x} error: {}", thread_id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_0(), **iter);
  }
  auto InstanceController::list_feed_comments(
    Writer<CommentDetail> out,
    ReadTxnBase& txn,
    uint64_t feed_id,
    SortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
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
        throw ApiError(fmt::format("No feed with ID {:x}", feed_id), 410);
    }
    const auto get_entry = [&](uint64_t id) -> optional<CommentDetail> {
      if (from && id == from.v) return {};
      const auto e = optional(CommentDetail::get(txn, id, login));
      return filter_comment(*e) ? e : nullopt;
    };
    optional<DBIter> iter;
    switch (sort) {
      case SortType::Active:
        return ranked_active<CommentDetail>(
          out,
          txn,
          txn.list_comments_new(),
          txn.list_comments_top(),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::Hot:
        return ranked_hot<CommentDetail>(
          out,
          txn,
          txn.list_comments_new(),
          txn.list_comments_top(),
          get_entry,
          from.rank_k(),
          limit
        );
      case SortType::NewComments:
        return ranked_new_comments<CommentDetail>(
          out,
          txn,
          txn.list_comments_new(new_comments_cursor(from)),
          get_entry,
          from ? optional(SECONDS(from.k)) : nullopt,
          limit
        );
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
        const auto time = SECONDS(entry.comment().created_at());
        if (time < earliest || !entry.should_show(login)) continue;
        out(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Comment {:x} error: {}", comment_id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_0(), **iter);
  }
  auto InstanceController::list_user_threads(
    Writer<ThreadDetail> out,
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
    const auto user = txn.get_user(user_id);
    if (!user) throw ApiError("User does not exist", 410);
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
        if (entry.should_fetch_card()) {
          event_bus->dispatch(Event::ThreadFetchLinkCard, entry.id);
        }
        out(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Thread {:x} error: {}", thread_id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_1(), **iter);
  }
  auto InstanceController::list_user_comments(
    Writer<CommentDetail> out,
    ReadTxnBase& txn,
    uint64_t user_id,
    UserPostSortType sort,
    Login login,
    PageCursor from,
    uint16_t limit
  ) -> PageCursor {
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
        out(entry);
      } catch (const ApiError& e) {
        spdlog::warn("Comment {:x} error: {}", comment_id, e.what());
      }
      if (--limit == 0) break;
    }
    if (iter->is_done()) return {};
    return PageCursor(iter->get_cursor()->int_field_1(), **iter);
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

  auto InstanceController::first_run_setup(FirstRunSetup&& update) -> void {
    const auto now = now_s();
    auto txn = db->open_write_txn();
    if (!txn.get_setting_int(SettingsKey::setup_done)) {
      if (!txn.get_setting_int(SettingsKey::next_id)) txn.set_setting(SettingsKey::next_id, 1);

      // JWT secret
      uint8_t jwt_secret[JWT_SECRET_SIZE];
      RAND_bytes(jwt_secret, JWT_SECRET_SIZE);
      txn.set_setting(SettingsKey::jwt_secret, string_view{(const char*)jwt_secret, JWT_SECRET_SIZE});
      OPENSSL_cleanse(jwt_secret, JWT_SECRET_SIZE);

      // RSA keys
      const unique_ptr<EVP_PKEY_CTX, void(*)(EVP_PKEY_CTX*)> kctx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr),
        EVP_PKEY_CTX_free
      );
      EVP_PKEY* key_ptr = nullptr;
      if (
        !kctx ||
        !EVP_PKEY_keygen_init(kctx.get()) ||
        !EVP_PKEY_CTX_set_rsa_keygen_bits(kctx.get(), 2048) ||
        !EVP_PKEY_keygen(kctx.get(), &key_ptr)
      ) {
        throw ApiError("RSA key generation failed (keygen init)", 500);
      }
      const unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)> key(key_ptr, EVP_PKEY_free);
      const unique_ptr<BIO, int(*)(BIO*)>
        bio_public(BIO_new(BIO_s_mem()), BIO_free),
        bio_private(BIO_new(BIO_s_mem()), BIO_free);
      if (
        PEM_write_bio_PUBKEY(bio_public.get(), key.get()) != 1 ||
        PEM_write_bio_PrivateKey(bio_private.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1
      ) {
        throw ApiError("RSA key generation failed (PEM generation)", 500);
      }
      const uint8_t* bio_data;
      size_t bio_len;
      if (!BIO_flush(bio_public.get()) || !BIO_mem_contents(bio_public.get(), &bio_data, &bio_len)) {
        throw ApiError("RSA key generation failed (write public)", 500);
      }
      txn.set_setting(SettingsKey::public_key, string_view{(const char*)bio_data, bio_len});
      if (!BIO_flush(bio_private.get()) || !BIO_mem_contents(bio_private.get(), &bio_data, &bio_len)) {
        throw ApiError("RSA key generation failed (write private)", 500);
      }
      txn.set_setting(SettingsKey::private_key, string_view{(const char*)bio_data, bio_len});

      txn.set_setting(SettingsKey::base_url, update.base_url.value_or("http://localhost:2023"));
      txn.set_setting(SettingsKey::media_upload_enabled, 0);
      txn.set_setting(SettingsKey::federation_enabled, 0);
      txn.set_setting(SettingsKey::federate_cw_content, 0);
      txn.set_setting(SettingsKey::setup_done, 1);
      txn.set_setting(SettingsKey::created_at, now);
    }
    if (update.admin_name && update.admin_password) {
      create_local_user_internal(
        txn, *update.admin_name, {}, std::move(*update.admin_password), false, IsApproved::Yes, IsAdmin::Yes, {}
      );
    }
    if (update.default_board_name) {
      if (!regex_match(update.default_board_name->begin(), update.default_board_name->end(), username_regex)) {
        throw ApiError("Invalid board name (only letters, numbers, and underscores allowed; max 64 characters)", 400);
      }
      if (txn.get_board_id_by_name(*update.default_board_name)) {
        throw ApiError("A board with this name already exists on this instance", 409);
      }
      FlatBufferBuilder fbb;
      {
        const auto name_s = fbb.CreateString(*update.default_board_name);
        BoardBuilder b(fbb);
        b.add_created_at(now_s());
        b.add_name(name_s);
        fbb.Finish(b.Finish());
      }
      const auto board_id = txn.create_board(fbb.GetBufferSpan());
      if (search_engine) {
        (*search_engine)->index(board_id, *GetRoot<Board>(fbb.GetBufferPointer()));
      }
      fbb.Clear();
      {
        LocalBoardBuilder b(fbb);
        b.add_owner(txn.get_admin_list()[0]);
        fbb.Finish(b.Finish());
      }
      txn.set_local_board(board_id, fbb.GetBufferSpan());
    }
    txn.set_setting(SettingsKey::name, update.name.value_or("Ludwig"));
    txn.set_setting(SettingsKey::description, update.description.value_or("A new Ludwig server"));
    txn.set_setting(SettingsKey::icon_url, update.icon_url.value_or("").value_or(""));
    txn.set_setting(SettingsKey::banner_url, update.banner_url.value_or("").value_or(""));
    txn.set_setting(SettingsKey::application_question, update.application_question.value_or("").value_or(""));
    txn.set_setting(SettingsKey::post_max_length, update.max_post_length.value_or(MiB));
    txn.set_setting(SettingsKey::home_page_type, (uint64_t)update.home_page_type.value_or(HomePageType::Local));
    txn.set_setting(SettingsKey::votes_enabled, update.votes_enabled.value_or(false));
    txn.set_setting(SettingsKey::downvotes_enabled, update.downvotes_enabled.value_or(false));
    txn.set_setting(SettingsKey::javascript_enabled, update.javascript_enabled.value_or(false));
    txn.set_setting(SettingsKey::infinite_scroll_enabled, update.infinite_scroll_enabled.value_or(false));
    txn.set_setting(SettingsKey::board_creation_admin_only, update.board_creation_admin_only.value_or(true));
    txn.set_setting(SettingsKey::registration_enabled, update.registration_enabled.value_or(false));
    txn.set_setting(SettingsKey::registration_application_required, update.registration_application_required.value_or(false));
    txn.set_setting(SettingsKey::registration_invite_required, update.registration_invite_required.value_or(false));
    txn.set_setting(SettingsKey::invite_admin_only, update.invite_admin_only.value_or(false));
    txn.set_setting(SettingsKey::updated_at, now);
    txn.commit();
    event_bus->dispatch(Event::SiteUpdate);
  }
  auto InstanceController::first_run_setup_options(ReadTxnBase& txn) -> FirstRunSetupOptions {
    return {
      .admin_exists = !txn.get_admin_list().empty(),
      .default_board_exists = !!txn.get_setting_int(SettingsKey::default_board_id),
      .base_url_set = !txn.get_setting_str(SettingsKey::base_url).empty(),
      .home_page_type_set = !!txn.get_setting_int(SettingsKey::home_page_type),
    };
  }
  auto InstanceController::update_site(const SiteUpdate& update, optional<uint64_t> as_user) -> void {
    {
      auto txn = db->open_write_txn();
      if (as_user && !can_change_site_settings(LocalUserDetail::get_login(txn, *as_user))) {
        throw ApiError("User does not have permission to change site settings", 403);
      }
      if (const auto v = update.name) txn.set_setting(SettingsKey::name, *v);
      if (const auto v = update.description) txn.set_setting(SettingsKey::description, *v);
      if (const auto v = update.icon_url) txn.set_setting(SettingsKey::icon_url, v->value_or(""));
      if (const auto v = update.banner_url) txn.set_setting(SettingsKey::banner_url, v->value_or(""));
      if (const auto v = update.application_question) txn.set_setting(SettingsKey::application_question, v->value_or(""));
      if (const auto v = update.max_post_length) txn.set_setting(SettingsKey::post_max_length, *v);
      if (const auto v = update.home_page_type) txn.set_setting(SettingsKey::home_page_type, (uint64_t)*v);
      if (const auto v = update.votes_enabled) txn.set_setting(SettingsKey::votes_enabled, *v);
      if (const auto v = update.downvotes_enabled) txn.set_setting(SettingsKey::downvotes_enabled, *v);
      if (const auto v = update.javascript_enabled) txn.set_setting(SettingsKey::javascript_enabled, *v);
      if (const auto v = update.infinite_scroll_enabled) txn.set_setting(SettingsKey::infinite_scroll_enabled, *v);
      if (const auto v = update.board_creation_admin_only) txn.set_setting(SettingsKey::board_creation_admin_only, *v);
      if (const auto v = update.registration_enabled) txn.set_setting(SettingsKey::registration_enabled, *v);
      if (const auto v = update.registration_application_required) txn.set_setting(SettingsKey::registration_application_required, *v);
      if (const auto v = update.registration_invite_required) txn.set_setting(SettingsKey::registration_invite_required, *v);
      if (const auto v = update.invite_admin_only) txn.set_setting(SettingsKey::invite_admin_only, *v);
      txn.set_setting(SettingsKey::updated_at, now_s());
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
    bool is_bot,
    IsApproved is_approved,
    IsAdmin is_admin,
    optional<uint64_t> invite
  ) -> uint64_t {
    if (!regex_match(username.begin(), username.end(), username_regex)) {
      throw ApiError("Invalid username (only letters, numbers, and underscores allowed; max 64 characters)", 400);
    }
    if (email && !regex_match(email->begin(), email->end(), email_regex)) {
      throw ApiError("Invalid email address", 400);
    }
    if (password.data.length() < 8) {
      throw ApiError("Password must be at least 8 characters", 400);
    }
    if (txn.get_user_id_by_name(username)) {
      throw ApiError("A user with this name already exists on this instance", 409);
    }
    if (email && txn.get_user_id_by_email(*email)) {
      throw ApiError("A user with this email address already exists on this instance", 409);
    }
    uint8_t salt[16], hash[32];
    if (!RAND_bytes(salt, 16)) {
      throw ApiError("Not enough randomness to generate secure password salt", 500);
    }
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
      b.add_approved(is_approved == IsApproved::Yes);
      b.add_admin(is_admin == IsAdmin::Yes);
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
      txn, username, email, std::move(password), false, approved ? IsApproved::Yes : IsApproved::No, IsAdmin::No, invite_id
    );
    if (invite_id) {
      const auto invite_opt = txn.get_invite(*invite_id);
      if (!invite_opt) throw ApiError("Invalid invite code", 410);
      const auto& invite = invite_opt->get();
      if (invite.accepted_at()) {
        spdlog::warn("Attempt to use already-used invite code {} (for username {}, email {}, ip {}, user agent {})",
          invite_id_to_code(*invite_id), username, email, ip, user_agent
        );
        throw ApiError("Expired invite code", 410);
      }
      const auto now = now_s();
      if (invite.expires_at() <= now) throw ApiError("Expired invite code", 410);
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
      txn, username, email, std::move(password), is_bot, IsApproved::Yes, IsAdmin::No, invite
    );
    txn.commit();
    return user_id;
  }

  auto InstanceController::update_local_user(uint64_t id, optional<uint64_t> as_user, const LocalUserUpdate& update) -> void {
    auto txn = db->open_write_txn();
    const auto login = LocalUserDetail::get_login(txn, as_user);
    const auto detail = LocalUserDetail::get(txn, id, login);
    if (login && !detail.can_change_settings(login)) {
      throw ApiError("User does not have permission to modify this user", 403);
    }
    if (update.email && !regex_match(update.email->cbegin(), update.email->cend(), email_regex)) {
      throw ApiError("Invalid email address", 400);
    }
    if (update.email && txn.get_user_id_by_email(string(*update.email))) {
      throw ApiError("A user with this email address already exists on this instance", 409);
    }
    if (update.display_name && *update.display_name && (*update.display_name)->length() > 1024) {
      throw ApiError("Display name cannot be longer than 1024 bytes", 400);
    }
    if (update.email || update.open_links_in_new_tab || update.show_avatars ||
        update.show_bot_accounts || update.hide_cw_posts ||
        update.expand_cw_posts || update.expand_cw_images ||
        update.show_karma || update.javascript_enabled ||
        update.infinite_scroll_enabled || update.default_sort_type ||
        update.default_comment_sort_type) {
      FlatBufferBuilder fbb;
      fbb.Finish(patch_local_user(fbb, detail.local_user(), {
        .email = update.email,
        .open_links_in_new_tab = update.open_links_in_new_tab,
        .show_avatars = update.show_avatars,
        .show_bot_accounts = update.show_bot_accounts,
        .hide_cw_posts = update.hide_cw_posts,
        .expand_cw_posts = update.expand_cw_posts,
        .expand_cw_images = update.expand_cw_images,
        .show_karma = update.show_karma,
        .javascript_enabled = update.javascript_enabled,
        .infinite_scroll_enabled = update.infinite_scroll_enabled,
        .default_sort_type = update.default_sort_type,
        .default_comment_sort_type = update.default_comment_sort_type
      }));
      txn.set_local_user(id, fbb.GetBufferSpan());
    }
    if (update.display_name || update.bio || update.avatar_url || update.banner_url || update.bot) {
      FlatBufferBuilder fbb;
      fbb.Finish(patch_user(fbb, *rich_text, detail.user(), {
        .display_name = update.display_name,
        .bio = update.bio,
        .avatar_url = update.avatar_url,
        .banner_url = update.banner_url,
        .updated_at = now_s(),
        .bot = update.bot
      }));
      txn.set_user(id, fbb.GetBufferSpan());
    }
    txn.commit();
  }
  auto InstanceController::approve_local_user_application(uint64_t user_id, optional<uint64_t> as_user) -> void {
    FlatBufferBuilder fbb;
    LocalUserPatch patch { .accepted_application = true };
    auto txn = db->open_write_txn();
    if (as_user && !LocalUserDetail::get_login(txn, *as_user).local_user().admin()) {
      throw ApiError("Only admins can approve user applications", 403);
    }
    const auto old_opt = txn.get_local_user(user_id);
    if (!old_opt) throw ApiError("User does not exist", 410);
    const auto& old = old_opt->get();
    if (old.accepted_application()) throw ApiError("User's application has already been accepted", 409);
    if (!txn.get_application(user_id)) throw ApiError("User does not have an application to approve", 410);
    fbb.Finish(patch_local_user(fbb, old, {
      .approved = old.approved() || site_detail()->registration_application_required,
      .accepted_application = true
    }));
    txn.set_local_user(user_id, fbb.GetBufferSpan());
    txn.commit();
  }
  auto InstanceController::reset_password(uint64_t user_id) -> string {
    // TODO: Reset password
    throw ApiError("Reset password is not yet supported", 500);
  }
  auto InstanceController::change_password(uint64_t user_id, SecretString&& new_password) -> void {
    auto txn = db->open_write_txn();
    const auto user = LocalUserDetail::get_login(txn, user_id);
    FlatBufferBuilder fbb;
    fbb.Finish(patch_local_user(fbb, user.local_user(), { .password = std::move(new_password) }));
    txn.set_local_user(user_id, fbb.GetBufferSpan());
  }
  auto InstanceController::change_password(string_view reset_token, SecretString&& new_password) -> string {
    // TODO: Reset password
    throw ApiError("Reset password is not yet supported", 500);
  }
  auto InstanceController::change_password(uint64_t user_id, SecretString&& old_password, SecretString&& new_password) -> void {
    auto txn = db->open_write_txn();
    const auto user = LocalUserDetail::get_login(txn, user_id);
    uint8_t hash[32];
    hash_password(std::move(old_password), user.local_user().password_salt()->bytes()->Data(), hash);
    // Note that this returns 0 on success, 1 on failure!
    if (CRYPTO_memcmp(hash, user.local_user().password_hash()->bytes()->Data(), 32)) {
      throw ApiError("Old password incorrect", 400);
    }
    FlatBufferBuilder fbb;
    fbb.Finish(patch_local_user(fbb, user.local_user(), { .password = std::move(new_password) }));
    txn.set_local_user(user_id, fbb.GetBufferSpan());
  }
  auto InstanceController::create_site_invite(optional<uint64_t> as_user) -> uint64_t {
    using namespace chrono;
    auto txn = db->open_write_txn();
    if (const auto user = LocalUserDetail::get_login(txn, as_user)) {
      if (site_detail()->invite_admin_only && !user->local_user().admin()) {
        throw ApiError("Only admins can create invite codes", 403);
      }
      if (user->mod_state() >= ModState::Locked) {
        throw ApiError("User does not have permission to create invite codes", 403);
      }
    }
    const auto id = txn.create_invite(as_user.value_or(0), duration_cast<seconds>(weeks{1}).count());
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
    if (!can_create_board(LocalUserDetail::get_login(txn, owner))) {
      throw ApiError("User does not have permission to create boards", 403);
    }
    FlatBufferBuilder fbb;
    {
      const auto [display_name_types, display_name_values] =
        display_name ? rich_text->parse_plain_text_with_emojis(fbb, *display_name) : pair(0, 0);
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
  auto InstanceController::update_local_board(uint64_t id, optional<uint64_t> as_user, const LocalBoardUpdate& update) -> void {
    auto txn = db->open_write_txn();
    const auto login = LocalUserDetail::get_login(txn, as_user);
    const auto detail = LocalBoardDetail::get(txn, id, login);
    if (login && !detail.can_change_settings(login)) {
      throw ApiError("User does not have permission to modify this board", 403);
    }
    if (update.display_name && *update.display_name && (*update.display_name)->length() > 1024) {
      throw ApiError("Display name cannot be longer than 1024 bytes", 400);
    }
    if (update.is_private || update.invite_required || update.invite_mod_only) {
      FlatBufferBuilder fbb;
      fbb.Finish(patch_local_board(fbb, detail.local_board(), {
        .private_ = update.is_private,
        .invite_required = update.invite_required,
        .invite_mod_only = update.invite_mod_only
      }));
      txn.set_local_board(id, fbb.GetBufferSpan());
    }
    if (update.display_name || update.description || update.icon_url || update.banner_url) {
      FlatBufferBuilder fbb;
      fbb.Finish(patch_board(fbb, *rich_text, detail.board(), {
        .display_name = update.display_name,
        .description = update.description,
        .icon_url = update.icon_url,
        .banner_url = update.banner_url
      }));
      txn.set_board(id, fbb.GetBufferSpan());
    }
    txn.commit();
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
      if (len > MiB) {
        throw ApiError("Post text content cannot be larger than 1MiB", 400);
      } else if (len < 1) {
        text_content_markdown = {};
      }
    }
    if (!submission_url && !text_content_markdown) {
      throw ApiError("Post must contain either a submission URL or text content", 400);
    }
    auto len = title.length();
    if (len > 1024) throw ApiError("Post title cannot be longer than 1024 bytes", 400);
    else if (len < 1) throw ApiError("Post title cannot be blank", 400);
    uint64_t thread_id;
    {
      auto txn = db->open_write_txn();
      const auto user = LocalUserDetail::get_login(txn, author);
      if (!BoardDetail::get(txn, board, user).can_create_thread(user)) {
        throw ApiError("User cannot create a thread in this board", 403);
      }
      FlatBufferBuilder fbb;
      const auto submission_s = submission_url ? fbb.CreateString(*submission_url) : 0,
        content_raw_s = text_content_markdown ? fbb.CreateString(*text_content_markdown) : 0,
        content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0;
      auto [title_blocks_type, title_blocks] = rich_text->parse_plain_text_with_emojis(fbb, title);
      pair<Offset<Vector<TextBlock>>, Offset<Vector<Offset<void>>>> content_blocks;
      if (text_content_markdown) content_blocks = rich_text->parse_markdown(fbb, *text_content_markdown);
      ThreadBuilder b(fbb);
      b.add_created_at(now_s());
      b.add_author(author);
      b.add_board(board);
      b.add_title_type(title_blocks_type);
      b.add_title(title_blocks);
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
  auto InstanceController::update_local_thread(uint64_t id, optional<uint64_t> as_user, const ThreadUpdate& update) -> void {
    auto txn = db->open_write_txn();
    const auto login = LocalUserDetail::get_login(txn, as_user);
    const auto detail = ThreadDetail::get(txn, id, login);
    if (detail.thread().instance()) {
      throw ApiError("Cannot edit a thread from a different instance", 403);
    }
    if (login && !detail.can_edit(login)) {
      throw ApiError("User does not have permission to edit this thread", 403);
    }
    if (update.title && update.title->empty()) {
      throw ApiError("Title cannot be empty", 400);
    }
    FlatBufferBuilder fbb;
    fbb.Finish(patch_thread(fbb, *rich_text, detail.thread(), {
      .title = update.title,
      .content_text = update.text_content,
      .content_warning = update.content_warning,
      .updated_at = now_s()
    }));
    txn.set_thread(id, fbb.GetBufferSpan());
    txn.commit();
  }
  auto InstanceController::create_local_comment(
    uint64_t author,
    uint64_t parent,
    string_view text_content_markdown,
    optional<string_view> content_warning
  ) -> uint64_t {
    auto len = text_content_markdown.length();
    if (len > MiB) throw ApiError("Comment text content cannot be larger than 1MiB", 400);
    else if (len < 1) throw ApiError("Comment text content cannot be blank", 400);
    auto txn = db->open_write_txn();
    const auto login = LocalUserDetail::get_login(txn, author);
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
    const auto [content_type, content] = rich_text->parse_markdown(fbb, text_content_markdown);
    CommentBuilder b(fbb);
    b.add_created_at(now_s());
    b.add_author(author);
    b.add_thread(parent_thread->id);
    b.add_parent(parent);
    b.add_content_raw(content_raw_s);
    b.add_content_type(content_type);
    b.add_content(content);
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
  auto InstanceController::update_local_comment(uint64_t id, optional<uint64_t> as_user, const CommentUpdate& update) -> void {
    auto txn = db->open_write_txn();
    const auto login = LocalUserDetail::get_login(txn, as_user);
    const auto detail = CommentDetail::get(txn, id, login);
    if (detail.comment().instance()) {
      throw ApiError("Cannot edit a comment from a different instance", 403);
    }
    if (login && !detail.can_edit(login)) {
      throw ApiError("User does not have permission to edit this comment", 403);
    }
    if (update.text_content && update.text_content->empty()) {
      throw ApiError("Content cannot be empty", 400);
    }
    FlatBufferBuilder fbb;
    fbb.Finish(patch_comment(fbb, *rich_text, detail.comment(), {
      .content = update.text_content,
      .content_warning = update.content_warning,
      .updated_at = now_s()
    }));
    txn.set_comment(id, fbb.GetBufferSpan());
    txn.commit();
  }
  auto InstanceController::vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_user(user_id)) throw ApiError("User does not exist", 410);
    const auto thread = txn.get_thread(post_id);
    const auto comment = !thread ? txn.get_comment(post_id) : nullopt;
    if (!thread && !comment) throw ApiError("Post does not exist", 410);
    const auto op = thread ? thread->get().author() : comment->get().author();
    txn.set_vote(user_id, post_id, vote);
    txn.commit();

    event_bus->dispatch(Event::UserStatsUpdate, op);
    event_bus->dispatch(Event::PostStatsUpdate, post_id);
  }
  auto InstanceController::subscribe(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_user(user_id)) throw ApiError("User does not exist", 410);
    if (!txn.get_board(board_id)) throw ApiError("Board does not exist", 410);
    txn.set_subscription(user_id, board_id, subscribed);
    txn.commit();

    event_bus->dispatch(Event::UserStatsUpdate, user_id);
    event_bus->dispatch(Event::BoardStatsUpdate, board_id);
  }
  auto InstanceController::save_post(uint64_t user_id, uint64_t post_id, bool saved) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) throw ApiError("User does not exist", 410);
    if (!txn.get_post_stats(post_id)) throw ApiError("Post does not exist", 410);
    txn.set_save(user_id, post_id, saved);
    txn.commit();
  }
  auto InstanceController::hide_post(uint64_t user_id, uint64_t post_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) throw ApiError("User does not exist", 410);
    if (!txn.get_post_stats(post_id)) throw ApiError("Post does not exist", 410);
    txn.set_hide_post(user_id, post_id, hidden);
    txn.commit();
  }
  auto InstanceController::hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id) || !txn.get_user(hidden_user_id)) {
      throw ApiError("User does not exist", 410);
    }
    txn.set_hide_user(user_id, hidden_user_id, hidden);
    txn.commit();
  }
  auto InstanceController::hide_board(uint64_t user_id, uint64_t board_id, bool hidden) -> void {
    auto txn = db->open_write_txn();
    if (!txn.get_local_user(user_id)) throw ApiError("User does not exist", 410);
    if (!txn.get_post_stats(board_id)) throw ApiError("Board does not exist", 410);
    txn.set_hide_post(user_id, board_id, hidden);
    txn.commit();
  }
}
