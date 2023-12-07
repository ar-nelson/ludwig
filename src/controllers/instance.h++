#pragma once
#include "util/web.h++"
#include "util/rich_text.h++"
#include "models/db.h++"
#include "models/detail.h++"
#include "services/db.h++"
#include "services/event_bus.h++"
#include "services/http_client.h++"
#include "services/search_engine.h++"
#include <atomic>
#include <map>
#include <regex>
#include <variant>
#include <static_vector.hpp>
#include <openssl/crypto.h>

namespace Ludwig {
  constexpr size_t ITEMS_PER_PAGE = 20;

  struct LoginResponse {
    uint64_t user_id, session_id;
    std::chrono::system_clock::time_point expiration;
  };

  struct PageCursor {
    bool exists = false;
    uint64_t k, v;

    PageCursor() : exists(false) {}
    explicit PageCursor(uint64_t k) : exists(true), k(k) {}
    PageCursor(uint64_t k, uint64_t v) : exists(true), k(k), v(v) {}
    PageCursor(double k, uint64_t v) : exists(true), k(*reinterpret_cast<uint64_t*>(&k)), v(v) {}
    PageCursor(std::string_view str) {
      static const std::regex re(R"(^([0-9a-f]+)(?:_([0-9a-f]+))?$)", std::regex::icase);
      if (str.empty()) return;
      std::match_results<std::string_view::const_iterator> match;
      if (std::regex_match(str.begin(), str.end(), match, re)) {
        exists = true;
        k = std::stoull(std::string(match[1].str()), nullptr, 16);
        if (match[2].matched) v = std::stoull(std::string(match[2].str()), nullptr, 16);
      } else {
        throw ApiError(fmt::format("Invalid cursor: {}", str), 400);
      }
    }

    operator bool() const noexcept { return exists; }
    auto to_string() const noexcept -> std::string {
      if (!exists) return "";
      if (!v) return fmt::format("{:x}", k);
      return fmt::format("{:x}_{:x}", k, v);
    }

    using OptKV = std::optional<std::pair<Cursor, uint64_t>>;

    inline auto rank_k() -> double {
      return exists ? *reinterpret_cast<double*>(&k) : INFINITY;
    }
    inline auto next_cursor_desc() -> OptKV {
      if (!exists) return {};
      return {{Cursor(k), v ? v - 1 : v}};
    }
    inline auto next_cursor_asc() -> OptKV {
      if (!exists) return {};
      return {{Cursor(k), v ? v + 1 : ID_MAX}};
    }
    inline auto next_cursor_desc(uint64_t prefix) -> OptKV {
      if (!exists) return {};
      return {{Cursor(prefix, k), v ? v - 1 : v}};
    }
    inline auto next_cursor_asc(uint64_t prefix) -> OptKV {
      if (!exists) return {};
      return {{Cursor(prefix, k), v ? v + 1 : ID_MAX}};
    }
  };

  template <typename T> struct PageOf {
    stlpb::static_vector<T, ITEMS_PER_PAGE> entries;
    bool is_first;
    PageCursor next;
  };

  struct CommentTree {
    std::unordered_map<uint64_t, PageCursor> continued;
    std::multimap<uint64_t, CommentDetail> comments;

    inline auto size() const -> size_t {
      return comments.size();
    }
    inline auto emplace(uint64_t parent, CommentDetail e) -> void {
      comments.emplace(parent, e);
    }
    inline auto mark_continued(uint64_t parent, PageCursor from = {}) {
      continued.emplace(parent, from);
    }
  };

  struct SiteUpdate {
    std::optional<std::string_view> name, description;
    std::optional<std::optional<std::string_view>> icon_url, banner_url;
    std::optional<uint64_t> max_post_length;
    std::optional<bool> javascript_enabled, board_creation_admin_only,
         registration_enabled, registration_application_required,
         registration_invite_required, invite_admin_only;
  };

  struct LocalUserUpdate {
    std::optional<const char*> email;
    std::optional<std::optional<std::string_view>> display_name, bio, avatar_url, banner_url;
    std::optional<bool> approved, accepted_application, email_verified,
        open_links_in_new_tab, show_avatars, show_bot_accounts, show_karma,
        hide_cw_posts, expand_cw_images, expand_cw_posts, javascript_enabled;
  };

  struct LocalBoardUpdate {
    std::optional<std::optional<const char*>> display_name, description, icon_url, banner_url;
    std::optional<bool> is_private, restricted_posting, approve_subscribe, can_upvote, can_downvote;
  };

  struct ThreadUpdate {
    std::optional<const char*> title;
    std::optional<std::optional<std::string_view>> text_content, content_warning;
  };

  struct CommentUpdate {
    std::optional<const char*> text_content;
    std::optional<std::optional<const char*>> content_warning;
  };

  static inline auto invite_code_to_id(std::string_view invite_code) -> uint64_t {
    static const std::regex invite_code_regex(R"(([0-9A-F]{5})-([0-9A-F]{3})-([0-9A-F]{3})-([0-9A-F]{5}))");
    std::match_results<std::string_view::const_iterator> match;
    if (std::regex_match(invite_code.begin(), invite_code.end(), match, invite_code_regex)) {
      return std::stoull(match[1].str() + match[2].str() + match[3].str() + match[4].str());
    }
    throw ApiError("Invalid invite code", 400);
  }

  static inline auto invite_id_to_code(uint64_t id) -> std::string {
    const auto s = fmt::format("{:016X}", id);
    const std::string_view v = s;
    return fmt::format("{}-{}-{}-{}", v.substr(0, 5), v.substr(5, 3), v.substr(8, 3), v.substr(11, 5));
  }

  static inline auto parse_sort_type(std::string_view str, Login login = {}) -> SortType {
    if (str.empty()) return login ? login->local_user().default_sort_type() : SortType::Active;
    if (str == "Hot") return SortType::Hot;
    if (str == "Active") return SortType::Active;
    if (str == "New") return SortType::New;
    if (str == "Old") return SortType::Old;
    if (str == "MostComments") return SortType::MostComments;
    if (str == "NewComments") return SortType::NewComments;
    if (str == "Top" || str == "TopAll") return SortType::TopAll;
    if (str == "TopYear") return SortType::TopYear;
    if (str == "TopSixMonths") return SortType::TopSixMonths;
    if (str == "TopThreeMonths") return SortType::TopThreeMonths;
    if (str == "TopMonth") return SortType::TopMonth;
    if (str == "TopWeek") return SortType::TopWeek;
    if (str == "TopDay") return SortType::TopDay;
    if (str == "TopTwelveHour") return SortType::TopTwelveHour;
    if (str == "TopSixHour") return SortType::TopSixHour;
    if (str == "TopHour") return SortType::TopHour;
    throw ApiError("Bad sort type", 400);
  }

  static inline auto parse_comment_sort_type(std::string_view str, Login login = {}) -> CommentSortType {
    if (str.empty()) return login ? login->local_user().default_comment_sort_type() : CommentSortType::Hot;
    if (str == "Hot") return CommentSortType::Hot;
    if (str == "New") return CommentSortType::New;
    if (str == "Old") return CommentSortType::Old;
    if (str == "Top") return CommentSortType::Top;
    throw ApiError("Bad comment sort type", 400);
  }

  static inline auto parse_user_post_sort_type(std::string_view str) -> UserPostSortType {
    if (str.empty() || str == "New") return UserPostSortType::New;
    if (str == "Old") return UserPostSortType::Old;
    if (str == "Top") return UserPostSortType::Top;
    throw ApiError("Bad post sort type", 400);
  }

  static inline auto parse_user_sort_type(std::string_view str) -> UserSortType {
    if (str.empty() || str == "NewPosts") return UserSortType::NewPosts;
    if (str == "MostPosts") return UserSortType::MostPosts;
    if (str == "New") return UserSortType::New;
    if (str == "Old") return UserSortType::Old;
    throw ApiError("Bad user sort type", 400);
  }

  static inline auto parse_board_sort_type(std::string_view str) -> BoardSortType {
    if (str.empty() || str == "MostSubscribers") return BoardSortType::MostSubscribers;
    if (str == "NewPosts") return BoardSortType::NewPosts;
    if (str == "MostPosts") return BoardSortType::MostPosts;
    if (str == "New") return BoardSortType::New;
    if (str == "Old") return BoardSortType::Old;
    throw ApiError("Bad board sort type", 400);
  }

  static auto parse_hex_id(std::string hex_id) -> std::optional<uint64_t> {
    if (hex_id.empty()) return {};
    try { return std::stoull(hex_id, nullptr, 16); }
    catch (...) { throw ApiError("Bad hexadecimal ID", 400); }
  }

  class InstanceController : public std::enable_shared_from_this<InstanceController> {
  private:
    std::shared_ptr<DB> db;
    std::shared_ptr<HttpClient> http_client;
    std::shared_ptr<RichTextParser> rich_text;
    std::shared_ptr<EventBus> event_bus;
    std::optional<std::shared_ptr<SearchEngine>> search_engine;
    std::atomic<const SiteDetail*> cached_site_detail;

    auto create_local_user_internal(
      WriteTxn& txn,
      std::string_view username,
      std::optional<std::string_view> email,
      SecretString&& password,
      bool is_approved,
      bool is_bot,
      std::optional<uint64_t> invite
    ) -> uint64_t;

    class SearchFunctor;
  public:
    static constexpr uint64_t FEED_ALL = 0, FEED_LOCAL = 1, FEED_HOME = 2;

    InstanceController(
      std::shared_ptr<DB> db,
      std::shared_ptr<HttpClient> http_client,
      std::shared_ptr<RichTextParser> rich_text,
      std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>(),
      std::optional<std::shared_ptr<SearchEngine>> search_engine = {}
    );
    ~InstanceController();

    using SearchResultDetail = std::variant<UserDetail, BoardDetail, ThreadDetail, CommentDetail>;

    static auto can_change_site_settings(Login login) -> bool;
    auto can_create_board(Login login) -> bool;

    inline auto open_read_txn() -> ReadTxnImpl {
      return db->open_read_txn();
    }

    static auto hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[32]) -> void;

    inline auto validate_session(ReadTxnBase& txn, uint64_t session_id) -> std::optional<uint64_t> {
      auto session = txn.get_session(session_id);
      if (session) return { session->get().user() };
      return {};
    }
    auto validate_or_regenerate_session(
      ReadTxnBase& txn,
      uint64_t session_id,
      std::string_view ip,
      std::string_view user_agent
    ) -> std::optional<LoginResponse>;
    auto login(
      std::string_view username,
      SecretString&& password,
      std::string_view ip,
      std::string_view user_agent,
      bool remember = false
    ) -> LoginResponse;
    inline auto site_detail() -> const SiteDetail* {
      return cached_site_detail;
    }
    auto thread_detail(
      ReadTxnBase& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      Login login = {},
      PageCursor from = {}
    ) -> std::pair<ThreadDetail, CommentTree>;
    auto comment_detail(
      ReadTxnBase& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      Login login = {},
      PageCursor from = {}
    ) -> std::pair<CommentDetail, CommentTree>;
    auto user_detail(ReadTxnBase& txn, uint64_t id, Login login) -> UserDetail;
    auto local_user_detail(ReadTxnBase& txn, uint64_t id, Login login) -> LocalUserDetail;
    auto board_detail(ReadTxnBase& txn, uint64_t id, Login login) -> BoardDetail;
    auto local_board_detail(ReadTxnBase& txn, uint64_t id, Login login) -> LocalBoardDetail;
    auto list_users(
      ReadTxnBase& txn,
      UserSortType sort,
      bool local_only,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<UserDetail>;
    auto list_boards(
      ReadTxnBase& txn,
      BoardSortType sort,
      bool local_only,
      bool subscribed_only,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<BoardDetail>;
    auto list_board_threads(
      ReadTxnBase& txn,
      uint64_t board_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<ThreadDetail>;
    auto list_board_comments(
      ReadTxnBase& txn,
      uint64_t board_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<CommentDetail>;
    auto list_feed_threads(
      ReadTxnBase& txn,
      uint64_t feed_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<ThreadDetail>;
    auto list_feed_comments(
      ReadTxnBase& txn,
      uint64_t feed_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<CommentDetail>;
    auto list_user_threads(
      ReadTxnBase& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<ThreadDetail>;
    auto list_user_comments(
      ReadTxnBase& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<CommentDetail>;
    auto search_step_1(SearchQuery query, SearchEngine::Callback&& callback) -> void;
    auto search_step_2(
      ReadTxnBase& txn,
      const std::vector<SearchResult>& results,
      size_t max_len,
      Login login
    ) -> std::vector<SearchResultDetail>;

    auto update_site(const SiteUpdate& update) -> void;
    auto register_local_user(
      std::string_view username,
      std::string_view email,
      SecretString&& password,
      std::string_view ip,
      std::string_view user_agent,
      std::optional<uint64_t> invite = {},
      std::optional<std::string_view> application_text = {}
    ) -> std::pair<uint64_t, bool>;
    auto create_local_user(
      std::string_view username,
      std::optional<std::string_view> email,
      SecretString&& password,
      bool is_bot,
      std::optional<uint64_t> invite = {}
    ) -> uint64_t;
    auto update_local_user(uint64_t id, const LocalUserUpdate& update) -> void;
    auto approve_local_user_application(uint64_t user_id) -> void;
    auto create_site_invite(uint64_t inviter_user_id) -> uint64_t;
    auto create_local_board(
      uint64_t owner,
      std::string_view name,
      std::optional<std::string_view> display_name,
      std::optional<std::string_view> content_warning = {},
      bool is_private = false,
      bool is_restricted_posting = false,
      bool is_local_only = false
    ) -> uint64_t;
    auto update_local_board(uint64_t id, const LocalBoardUpdate& update) -> void;
    auto create_local_thread(
      uint64_t author,
      uint64_t board,
      std::string_view title,
      std::optional<std::string_view> submission_url,
      std::optional<std::string_view> text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto update_thread(uint64_t id, const ThreadUpdate& update) -> void;
    auto create_local_comment(
      uint64_t author,
      uint64_t parent,
      std::string_view text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto update_comment(uint64_t id, const CommentUpdate& update) -> void;
    auto vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void;
    auto subscribe(uint64_t user_id, uint64_t board_id, bool subscribed = true) -> void;
    auto save_post(uint64_t user_id, uint64_t post_id, bool saved = true) -> void;
    auto hide_post(uint64_t user_id, uint64_t post_id, bool hidden = true) -> void;
    auto hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden = true) -> void;
    auto hide_board(uint64_t user_id, uint64_t board_id, bool hidden = true) -> void;
  };
}
