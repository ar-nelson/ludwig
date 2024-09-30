#pragma once
#include "util/router.h++"
#include "util/web.h++"
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
#include <openssl/crypto.h>

namespace Ludwig {
  constexpr size_t ITEMS_PER_PAGE = 20;

# define USERNAME_REGEX_SRC R"([a-zA-Z][a-zA-Z0-9_]{0,63})"

  static const std::regex
    username_regex(USERNAME_REGEX_SRC),
    email_regex(
      R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|")"
      R"((?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@)"
      R"((?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|)"
      R"(\[(?:(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9]))\.){3})"
      R"((?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])|[a-z0-9-]*[a-z0-9]:)"
      R"((?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))",
      std::regex::ECMAScript | std::regex::icase
    ),
    color_hex_regex(R"(#[0-9a-f]{6})", std::regex::icase);

  struct LoginResponse {
    uint64_t user_id, session_id;
    Timestamp expiration;
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

    auto rank_k() -> double {
      return exists ? *reinterpret_cast<double*>(&k) : INFINITY;
    }
    auto next_cursor_desc() -> OptKV {
      if (!exists) return {};
      return {{Cursor(k), v ? v - 1 : v}};
    }
    auto next_cursor_asc() -> OptKV {
      if (!exists) return {};
      return {{Cursor(k), v ? v + 1 : ID_MAX}};
    }
    auto next_cursor_desc(uint64_t prefix) -> OptKV {
      if (!exists) return {};
      return {{Cursor(prefix, k), v ? v - 1 : v}};
    }
    auto next_cursor_asc(uint64_t prefix) -> OptKV {
      if (!exists) return {};
      return {{Cursor(prefix, k), v ? v + 1 : ID_MAX}};
    }
  };

  template <typename T> using Writer = std::function<void (const T&)>;

  struct CommentTree {
    std::unordered_map<uint64_t, PageCursor> continued;
    std::multimap<uint64_t, CommentDetail> comments;

    auto size() const -> size_t {
      return comments.size();
    }
    auto emplace(uint64_t parent, CommentDetail e) -> void {
      comments.emplace(parent, e);
    }
    auto mark_continued(uint64_t parent, PageCursor from = {}) {
      continued.emplace(parent, from);
    }
  };

  struct SiteUpdate {
    std::optional<std::string_view> name, description, color_accent,
        color_accent_dim, color_accent_hover;
    std::optional<std::optional<std::string_view>> icon_url, banner_url,
        application_question;
    std::optional<uint64_t> post_max_length, remote_post_max_length;
    std::optional<HomePageType> home_page_type;
    std::optional<bool> javascript_enabled, infinite_scroll_enabled,
        votes_enabled, downvotes_enabled, cws_enabled, require_login_to_view,
        board_creation_admin_only, registration_enabled,
        registration_application_required, registration_invite_required,
        invite_admin_only;

    auto validate() const -> void {
      if (icon_url && *icon_url) {
        if (const auto url = Url::parse(std::string(**icon_url))) {
          if (!url->is_http_s()) throw ApiError("Icon URL must be HTTP(S)", 400);
        } else throw ApiError("Icon URL is not a valid URL", 400);
      }
      if (banner_url && *banner_url) {
        if (const auto url = Url::parse(std::string(**banner_url))) {
          if (!url->is_http_s()) throw ApiError("Banner URL must be HTTP(S)", 400);
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
  };

  struct FirstRunSetup : SiteUpdate {
    std::optional<std::string_view> base_url, default_board_name, admin_name;
    std::optional<SecretString> admin_password;
  };

  struct FirstRunSetupOptions {
    bool admin_exists, default_board_exists, base_url_set, home_page_type_set;
  };

  enum class IsApproved : bool { No, Yes };
  enum class IsAdmin : bool { No, Yes };

  struct LocalUserUpdate {
    std::optional<std::string_view> email;
    std::optional<std::optional<std::string_view>> display_name, bio,
        avatar_url, banner_url;
    std::optional<bool> bot, open_links_in_new_tab, show_avatars,
        show_bot_accounts, show_karma, hide_cw_posts, expand_cw_images,
        expand_cw_posts, javascript_enabled, infinite_scroll_enabled;
    std::optional<IsAdmin> admin;
    std::optional<SortType> default_sort_type;
    std::optional<CommentSortType> default_comment_sort_type;
  };

  struct LocalBoardUpdate {
    std::optional<std::optional<std::string_view>> display_name, description,
        icon_url, banner_url, content_warning;
    std::optional<bool> is_private, restricted_posting, approve_subscribe,
        invite_required, invite_mod_only, can_upvote, can_downvote;
  };

  struct ThreadUpdate {
    std::optional<std::string_view> title;
    std::optional<std::optional<std::string_view>> text_content, content_warning;
  };

  struct CommentUpdate {
    std::optional<std::string_view> text_content;
    std::optional<std::optional<std::string_view>> content_warning;
  };

  // static auto parse_hex_id(std::string hex_id) -> std::optional<uint64_t> {
  //   if (hex_id.empty()) return {};
  //   try { return std::stoull(hex_id, nullptr, 16); }
  //   catch (...) { throw ApiError("Bad hexadecimal ID", 400); }
  // }

  class InstanceController : public std::enable_shared_from_this<InstanceController> {
  private:
    std::shared_ptr<DB> db;
    std::shared_ptr<HttpClient> http_client;
    std::shared_ptr<EventBus> event_bus;
    EventBus::Subscription site_detail_sub;
    std::optional<std::shared_ptr<SearchEngine>> search_engine;
    std::optional<std::pair<Hash, Salt>> first_run_admin_password;
    std::atomic<const SiteDetail*> cached_site_detail;
    std::map<uint64_t, std::pair<uint64_t, Timestamp>> password_reset_tokens;
    std::mutex password_reset_tokens_mutex;

    auto create_local_user_internal(
      WriteTxn& txn,
      std::string_view username,
      std::optional<std::string_view> email,
      SecretString&& password,
      bool is_bot,
      IsApproved is_approved,
      IsAdmin is_admin,
      std::optional<uint64_t> invite
    ) -> uint64_t;
    auto create_thread_internal(
      WriteTxn& txn,
      uint64_t author,
      uint64_t board,
      std::optional<std::string_view> remote_post_url,
      std::optional<std::string_view> remote_activity_url,
      Timestamp created_at,
      std::optional<Timestamp> updated_at,
      std::string_view title,
      std::optional<std::string_view> submission_url,
      std::optional<std::string_view> text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto create_comment_internal(
      WriteTxn& txn,
      uint64_t author,
      uint64_t parent,
      uint64_t thread,
      std::optional<std::string_view> remote_post_url,
      std::optional<std::string_view> remote_activity_url,
      Timestamp created_at,
      std::optional<Timestamp> updated_at,
      std::string_view text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;

    class SearchFunctor;
  public:
    static constexpr uint64_t FEED_ALL = 0, FEED_LOCAL = 1, FEED_HOME = 2;

    InstanceController(
      std::shared_ptr<DB> db,
      std::shared_ptr<HttpClient> http_client,
      std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>(),
      std::optional<std::shared_ptr<SearchEngine>> search_engine = {},
      std::optional<std::pair<Hash, Salt>> first_run_admin_password = {}
    );
    ~InstanceController();

    using SearchResultDetail = std::variant<UserDetail, BoardDetail, ThreadDetail, CommentDetail>;

    static auto can_change_site_settings(Login login) -> bool;
    auto can_create_board(Login login) -> bool;

    auto open_read_txn() -> ReadTxnImpl {
      return db->open_read_txn();
    }
    template <IsRequestContext Ctx>
    auto open_write_txn() -> WriteTxnAwaiter<Ctx> {
      return WriteTxnAwaiter<Ctx>(*db);
    }

    static auto hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[32]) -> void;

    auto validate_session(ReadTxn& txn, uint64_t session_id) -> std::optional<uint64_t> {
      auto session = txn.get_session(session_id);
      if (session) return { session->get().user() };
      return {};
    }
    auto validate_or_regenerate_session(
      ReadTxn& txn,
      uint64_t session_id,
      std::string_view ip,
      std::string_view user_agent
    ) -> std::optional<LoginResponse>;
    auto delete_session(WriteTxn txn, uint64_t session_id) -> void {
      txn.delete_session(session_id);
      txn.commit();
    }
    auto login(
      WriteTxn txn,
      std::string_view username,
      SecretString&& password,
      std::string_view ip,
      std::string_view user_agent,
      bool remember = false
    ) -> LoginResponse;
    auto site_detail() -> const SiteDetail* {
      return cached_site_detail;
    }
    auto thread_detail(
      ReadTxn& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> std::pair<ThreadDetail, CommentTree>;
    auto comment_detail(
      ReadTxn& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> std::pair<CommentDetail, CommentTree>;
    auto user_detail(ReadTxn& txn, uint64_t id, Login login) -> UserDetail;
    auto local_user_detail(ReadTxn& txn, uint64_t id, Login login) -> LocalUserDetail;
    auto board_detail(ReadTxn& txn, uint64_t id, Login login) -> BoardDetail;
    auto local_board_detail(ReadTxn& txn, uint64_t id, Login login) -> LocalBoardDetail;
    auto list_users(
      Writer<UserDetail> out,
      ReadTxn& txn,
      UserSortType sort,
      bool local_only,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_applications(
      Writer<std::pair<const Application&, LocalUserDetail>> out,
      ReadTxn& txn,
      Login login = {},
      std::optional<uint64_t> from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> std::optional<uint64_t>;
    auto list_invites_from_user(
      Writer<std::pair<uint64_t, const Invite&>> out,
      ReadTxn& txn,
      uint64_t user_id,
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_boards(
      Writer<BoardDetail> out,
      ReadTxn& txn,
      BoardSortType sort,
      bool local_only,
      bool subscribed_only,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_board_threads(
      Writer<ThreadDetail> out,
      ReadTxn& txn,
      uint64_t board_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_board_comments(
      Writer<CommentDetail> out,
      ReadTxn& txn,
      uint64_t board_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_feed_threads(
      Writer<ThreadDetail> out,
      ReadTxn& txn,
      uint64_t feed_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_feed_comments(
      Writer<CommentDetail> out,
      ReadTxn& txn,
      uint64_t feed_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_user_threads(
      Writer<ThreadDetail> out,
      ReadTxn& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto list_user_comments(
      Writer<CommentDetail> out,
      ReadTxn& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      Login login = {},
      PageCursor from = {},
      uint16_t limit = ITEMS_PER_PAGE
    ) -> PageCursor;
    auto search_step_1(SearchQuery query, SearchEngine::Callback&& callback) -> void;
    auto search_step_2(
      ReadTxn& txn,
      const std::vector<SearchResult>& results,
      size_t max_len,
      Login login
    ) -> std::vector<SearchResultDetail>;
    auto first_run_setup_options(ReadTxn& txn) -> FirstRunSetupOptions;

    auto first_run_setup(WriteTxn txn, FirstRunSetup&& update) -> void;
    auto update_site(
      WriteTxn txn,
      const SiteUpdate& update,
      std::optional<uint64_t> as_user
    ) -> void;
    auto register_local_user(
      WriteTxn txn,
      std::string_view username,
      std::string_view email,
      SecretString&& password,
      std::string_view ip,
      std::string_view user_agent,
      std::optional<uint64_t> invite = {},
      std::optional<std::string_view> application_text = {}
    ) -> std::pair<uint64_t, bool>;
    auto create_local_user(
      WriteTxn txn,
      std::string_view username,
      std::optional<std::string_view> email,
      SecretString&& password,
      bool is_bot,
      std::optional<uint64_t> invite = {},
      IsApproved is_approved = IsApproved::No,
      IsAdmin is_admin = IsAdmin::No
    ) -> uint64_t;
    auto update_local_user(
      WriteTxn txn,
      uint64_t id,
      std::optional<uint64_t> as_user,
      const LocalUserUpdate& update
    ) -> void;
    auto reset_password(WriteTxn txn, uint64_t user_id) -> std::string;
    auto change_password(WriteTxn txn, uint64_t user_id, SecretString&& new_password) -> void;
    auto change_password(
      WriteTxn txn,
      std::string_view reset_token,
      SecretString&& new_password
    ) -> std::string; // returns username
    auto change_password(
      WriteTxn txn,
      uint64_t user_id,
      SecretString&& old_password,
      SecretString&& new_password
    ) -> void;
    auto approve_local_user_application(
      WriteTxn txn,
      uint64_t user_id,
      std::optional<uint64_t> as_user
    ) -> void;
    auto reject_local_user_application(
      WriteTxn txn,
      uint64_t user_id,
      std::optional<uint64_t> as_user
    ) -> void;
    auto create_site_invite(WriteTxn txn, std::optional<uint64_t> as_user) -> uint64_t;
    auto create_local_board(
      WriteTxn txn,
      uint64_t owner,
      std::string_view name,
      std::optional<std::string_view> display_name,
      std::optional<std::string_view> content_warning = {},
      bool is_private = false,
      bool is_restricted_posting = false,
      bool is_local_only = false
    ) -> uint64_t;
    auto update_local_board(
      WriteTxn txn,
      uint64_t id,
      std::optional<uint64_t> as_user,
      const LocalBoardUpdate& update
    ) -> void;
    auto create_thread(
      WriteTxn txn,
      uint64_t author,
      uint64_t board,
      std::optional<std::string_view> remote_post_url,
      std::optional<std::string_view> remote_activity_url,
      Timestamp created_at,
      std::optional<Timestamp> updated_at,
      std::string_view title,
      std::optional<std::string_view> submission_url,
      std::optional<std::string_view> text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto create_local_thread(
      WriteTxn txn,
      uint64_t author,
      uint64_t board,
      std::string_view title,
      std::optional<std::string_view> submission_url,
      std::optional<std::string_view> text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto update_thread(
      WriteTxn txn,
      uint64_t id,
      std::optional<uint64_t> as_user,
      const ThreadUpdate& update
    ) -> void;
    auto create_comment(
      WriteTxn txn,
      uint64_t author,
      uint64_t parent,
      std::optional<std::string_view> remote_post_url,
      std::optional<std::string_view> remote_activity_url,
      Timestamp created_at,
      std::optional<Timestamp> updated_at,
      std::string_view text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto create_local_comment(
      WriteTxn txn,
      uint64_t author,
      uint64_t parent,
      std::string_view text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto update_comment(
      WriteTxn txn,
      uint64_t id,
      std::optional<uint64_t> as_user,
      const CommentUpdate& update
    ) -> void;
    auto vote(
      WriteTxn txn,
      uint64_t user_id,
      uint64_t post_id,
      Vote vote
    ) -> void;
    auto subscribe(
      WriteTxn txn,
      uint64_t user_id,
      uint64_t board_id,
      bool subscribed = true
    ) -> void;
    auto save_post(
      WriteTxn txn,
      uint64_t user_id,
      uint64_t post_id,
      bool saved = true
    ) -> void;
    auto hide_post(
      WriteTxn txn,
      uint64_t user_id,
      uint64_t post_id,
      bool hidden = true
    ) -> void;
    auto hide_user(
      WriteTxn txn,
      uint64_t user_id,
      uint64_t hidden_user_id,
      bool hidden = true
    ) -> void;
    auto hide_board(
      WriteTxn txn,
      uint64_t user_id,
      uint64_t board_id,
      bool hidden = true
    ) -> void;
  };
}
