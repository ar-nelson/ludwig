#pragma once
#include "util/web.h++"
#include "models/db.h++"
#include "services/db.h++"
#include "services/http_client.h++"
#include "services/search_engine.h++"
#include <map>
#include <regex>
#include <variant>
#include <static_vector.hpp>
#include <openssl/crypto.h>

namespace Ludwig {
  constexpr size_t ITEMS_PER_PAGE = 20;

  struct SecretString {
    std::string_view str;
    SecretString(std::string_view str) : str(str) {};
    SecretString(const SecretString&) = delete;
    SecretString& operator=(const SecretString&) = delete;
    ~SecretString() {
      OPENSSL_cleanse((char*)str.data(), str.length());
    }
  };

  struct LoginResponse {
    uint64_t user_id, session_id, expiration;
  };

  struct SiteDetail {
    std::string name, domain, description;
    std::optional<std::string> icon_url, banner_url;
    uint64_t max_post_length;
    bool javascript_enabled, infinite_scroll_enabled, board_creation_admin_only,
         registration_enabled, registration_application_required,
         registration_invite_required, invite_admin_only;
  };

  struct UserListEntry {
    uint64_t id;
    std::reference_wrapper<const User> _user;
    OptRef<const LocalUser> _local_user;
    std::reference_wrapper<const UserStats> _stats;
    bool hidden;

    inline auto user() const -> const User& { return _user; }
    inline auto maybe_local_user() const -> OptRef<const LocalUser> { return _local_user; }
    inline auto stats() const -> const UserStats& { return _stats; }
  };

  struct BoardListEntry {
    uint64_t id;
    std::reference_wrapper<const Board> _board;
    std::reference_wrapper<const BoardStats> _stats;
    bool hidden, subscribed;

    inline auto board() const -> const Board& { return _board; }
    inline auto stats() const -> const BoardStats& { return _stats; }
  };

  struct ThreadListEntry {
  private:
    static const flatbuffers::FlatBufferBuilder null_user, null_board;
  public:
    uint64_t id;
    double rank;
    Vote your_vote;
    bool saved, hidden, user_hidden, board_hidden;
    std::reference_wrapper<const Thread> _thread;
    std::reference_wrapper<const PostStats> _stats;
    OptRef<User> _author;
    OptRef<Board> _board;

    inline auto thread() const -> const Thread& { return _thread; }
    inline auto stats() const -> const PostStats& { return _stats; }
    inline auto author() const -> const User& {
      return _author ? _author->get() : *flatbuffers::GetRoot<User>(null_user.GetBufferPointer());
    }
    inline auto board() const -> const Board& {
      return _board ? _board->get() : *flatbuffers::GetRoot<Board>(null_board.GetBufferPointer());
    }
  };

  struct CommentListEntry {
  private:
    static const flatbuffers::FlatBufferBuilder null_user, null_thread, null_board;
  public:
    uint64_t id;
    double rank;
    Vote your_vote;
    bool saved, hidden, thread_hidden, user_hidden, board_hidden;
    std::reference_wrapper<const Comment> _comment;
    std::reference_wrapper<const PostStats> _stats;
    OptRef<User> _author;
    OptRef<Thread> _thread;
    OptRef<Board> _board;

    inline auto comment() const -> const Comment& { return _comment; }
    inline auto stats() const -> const PostStats& { return _stats; }
    inline auto author() const -> const User& {
      return _author ? _author->get() : *flatbuffers::GetRoot<User>(null_user.GetBufferPointer());
    }
    inline auto thread() const -> const Thread& {
      return _thread ? _thread->get() : *flatbuffers::GetRoot<Thread>(null_thread.GetBufferPointer());
    }
    inline auto board() const -> const Board& {
      return _board ? _board->get() : *flatbuffers::GetRoot<Board>(null_board.GetBufferPointer());
    }
  };

  struct PageCursor {
    bool exists = false;
    uint64_t k, v;

    PageCursor() : exists(false) {}
    explicit PageCursor(uint64_t k) : exists(true), k(k) {}
    PageCursor(uint64_t k, uint64_t v) : exists(true), k(k), v(v) {}
    PageCursor(std::string_view str) {
      static const std::regex re(R"(^([0-9a-f]+)(?:_([0-9a-f]+))$)", std::regex::icase);
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
    std::multimap<uint64_t, CommentListEntry> comments;

    inline auto size() const -> size_t {
      return comments.size();
    }
    inline auto emplace(uint64_t parent, CommentListEntry e) -> void {
      comments.emplace(parent, e);
    }
    inline auto mark_continued(uint64_t parent, PageCursor from = {}) {
      continued.emplace(parent, from);
    }
  };

  struct LocalUserDetailResponse : UserListEntry {
    inline auto local_user() const -> const LocalUser& { return _local_user->get(); }
  };

  struct LocalBoardDetailResponse : BoardListEntry {
    std::reference_wrapper<const LocalBoard> _local_board;

    inline auto local_board() const -> const LocalBoard& { return _local_board; }
  };

  struct ThreadDetailResponse : ThreadListEntry {
    CommentTree comments;
  };

  struct CommentDetailResponse : CommentListEntry {
    CommentTree comments;
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
    std::optional<std::string_view> email;
    std::optional<std::optional<std::string_view>> display_name, bio, avatar_url, banner_url;
    std::optional<bool> open_links_in_new_tab, show_avatars, show_bot_accounts,
      show_karma, hide_cw_posts, expand_cw_images, expand_cw_posts, javascript_enabled;
  };

  struct LocalBoardUpdate {
    std::optional<std::optional<std::string_view>> display_name, description, icon_url, banner_url;
    std::optional<bool> is_private, restricted_posting, approve_subscribe, can_upvote, can_downvote;
  };

  struct ThreadUpdate {
    std::optional<std::string_view> title;
    std::optional<std::optional<std::string_view>> text_content, content_warning;
  };

  struct CommentUpdate {
    std::optional<std::string_view> text_content;
    std::optional<std::optional<std::string_view>> content_warning;
  };

  enum class Event : uint8_t {
    SiteUpdate,
    UserUpdate,
    UserStatsUpdate,
    LocalUserUpdate,
    UserDelete,
    BoardUpdate,
    BoardStatsUpdate,
    LocalBoardUpdate,
    BoardDelete,
    ThreadUpdate,
    ThreadDelete,
    CommentUpdate,
    CommentDelete,
    PostStatsUpdate,
    MaxEvent
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

  class InstanceController : public std::enable_shared_from_this<InstanceController> {
  private:
    std::shared_ptr<DB> db;
    std::shared_ptr<HttpClient> http_client;
    std::optional<std::shared_ptr<SearchEngine>> search_engine;
    SiteDetail cached_site_detail;

    auto fetch_link_preview(uint64_t thread_id, std::string_view url) -> void;
    auto create_local_user_internal(
      WriteTxn& txn,
      std::string_view username,
      std::optional<std::string_view> email,
      SecretString&& password,
      bool is_bot,
      std::optional<uint64_t> invite
    ) -> uint64_t;
    virtual auto dispatch_event(Event, uint64_t = 0) -> void {}

    class SearchFunctor;
  public:
    InstanceController(
      std::shared_ptr<DB> db,
      std::shared_ptr<HttpClient> http_client,
      std::optional<std::shared_ptr<SearchEngine>> search_engine = {}
    );
    virtual ~InstanceController() = default;

    using SearchResultListEntry = std::variant<UserListEntry, BoardListEntry, ThreadListEntry, CommentListEntry>;
    using SearchCallback = std::move_only_function<void (ReadTxnBase&, std::vector<SearchResultListEntry>)>;
    using Login = const std::optional<LocalUserDetailResponse>&;

    static auto parse_sort_type(std::string_view str, Login login = {}) -> SortType {
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

    static auto parse_comment_sort_type(std::string_view str, Login login = {}) -> CommentSortType {
      if (str.empty()) return login ? login->local_user().default_comment_sort_type() : CommentSortType::Hot;
      if (str == "Hot") return CommentSortType::Hot;
      if (str == "New") return CommentSortType::New;
      if (str == "Old") return CommentSortType::Old;
      if (str == "Top") return CommentSortType::Top;
      throw ApiError("Bad comment sort type", 400);
    }

    static auto parse_user_post_sort_type(std::string_view str) -> UserPostSortType {
      if (str.empty() || str == "New") return UserPostSortType::New;
      if (str == "Old") return UserPostSortType::Old;
      if (str == "Top") return UserPostSortType::Top;
      throw ApiError("Bad post sort type", 400);
    }

    static auto parse_user_sort_type(std::string_view str) -> UserSortType {
      if (str.empty() || str == "NewPosts") return UserSortType::NewPosts;
      if (str == "MostPosts") return UserSortType::MostPosts;
      if (str == "New") return UserSortType::New;
      if (str == "Old") return UserSortType::Old;
      throw ApiError("Bad user sort type", 400);
    }

    static auto parse_board_sort_type(std::string_view str) -> BoardSortType {
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

    static auto can_view(const ThreadListEntry& thread, Login login) -> bool;
    static auto can_view(const CommentListEntry& comment, Login login) -> bool;
    static auto can_view(const UserListEntry& user, Login login) -> bool;
    static auto can_view(const BoardListEntry& board, Login login) -> bool;
    static auto should_show(const ThreadListEntry& thread, Login login) -> bool;
    static auto should_show(const CommentListEntry& comment, Login login) -> bool;
    static auto should_show(const UserListEntry& user, Login login) -> bool;
    static auto should_show(const BoardListEntry& board, Login login) -> bool;
    static auto can_create_thread(const BoardListEntry& board, Login login) -> bool;
    static auto can_reply_to(const ThreadListEntry& thread, Login login) -> bool;
    static auto can_reply_to(const CommentListEntry& comment, Login login) -> bool;
    static auto can_edit(const ThreadListEntry& thread, Login login) -> bool;
    static auto can_edit(const CommentListEntry& comment, Login login) -> bool;
    static auto can_delete(const ThreadListEntry& thread, Login login) -> bool;
    static auto can_delete(const CommentListEntry& comment, Login login) -> bool;
    static auto can_upvote(const ThreadListEntry& thread, Login login) -> bool;
    static auto can_upvote(const CommentListEntry& comment, Login login) -> bool;
    static auto can_downvote(const ThreadListEntry& thread, Login login) -> bool;
    static auto can_downvote(const CommentListEntry& comment, Login login) -> bool;
    static auto can_change_board_settings(const LocalBoardDetailResponse& board, Login login) -> bool;
    static auto can_change_site_settings(Login login) -> bool;

    static auto get_thread_entry(
      ReadTxnBase& txn,
      uint64_t thread_id,
      InstanceController::Login login,
      OptRef<User> author = {},
      bool is_author_hidden = false,
      OptRef<Board> board = {},
      bool is_board_hidden = false
    ) -> ThreadListEntry;

    static auto get_comment_entry(
      ReadTxnBase& txn,
      uint64_t comment_id,
      Login login,
      OptRef<User> author = {},
      bool is_author_hidden = false,
      OptRef<Thread> thread = {},
      bool is_thread_hidden = false,
      OptRef<Board> board = {},
      bool is_board_hidden = false
    ) -> CommentListEntry;

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
      return &cached_site_detail;
    }
    auto thread_detail(
      ReadTxnBase& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      Login login = {},
      PageCursor from = {}
    ) -> ThreadDetailResponse;
    auto comment_detail(
      ReadTxnBase& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      Login login = {},
      PageCursor from = {}
    ) -> CommentDetailResponse;
    auto user_detail(ReadTxnBase& txn, uint64_t id, Login login) -> UserListEntry;
    auto local_user_detail(ReadTxnBase& txn, uint64_t id) -> LocalUserDetailResponse;
    auto board_detail(ReadTxnBase& txn, uint64_t id, Login login) -> BoardListEntry;
    auto local_board_detail(ReadTxnBase& txn, uint64_t id, Login login) -> LocalBoardDetailResponse;
    auto list_users(
      ReadTxnBase& txn,
      UserSortType sort,
      bool local_only,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<UserListEntry>;
    auto list_boards(
      ReadTxnBase& txn,
      BoardSortType sort,
      bool local_only,
      bool subscribed_only,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<BoardListEntry>;
    auto list_board_threads(
      ReadTxnBase& txn,
      uint64_t board_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<ThreadListEntry>;
    auto list_board_comments(
      ReadTxnBase& txn,
      uint64_t board_id,
      SortType sort = SortType::Active,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<CommentListEntry>;
    auto list_user_threads(
      ReadTxnBase& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<ThreadListEntry>;
    auto list_user_comments(
      ReadTxnBase& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      Login login = {},
      PageCursor from = {}
    ) -> PageOf<CommentListEntry>;
    auto search(SearchQuery query, Login login, SearchCallback callback) -> void;

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
    auto update_local_user(uint64_t id, LocalUserUpdate update) -> void;
    auto approve_local_user_application(uint64_t user_id) -> void;
    auto create_local_board(
      uint64_t owner,
      std::string_view name,
      std::optional<std::string_view> display_name,
      std::optional<std::string_view> content_warning = {},
      bool is_private = false,
      bool is_restricted_posting = false,
      bool is_local_only = false
    ) -> uint64_t;
    auto update_local_board(uint64_t id, LocalBoardUpdate update) -> void;
    auto create_local_thread(
      uint64_t author,
      uint64_t board,
      std::string_view title,
      std::optional<std::string_view> submission_url,
      std::optional<std::string_view> text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto update_thread(uint64_t id, ThreadUpdate update) -> void;
    auto create_local_comment(
      uint64_t author,
      uint64_t parent,
      std::string_view text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto update_comment(uint64_t id, CommentUpdate update) -> void;
    auto vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void;
    auto subscribe(uint64_t user_id, uint64_t board_id, bool subscribed = true) -> void;
    auto save_post(uint64_t user_id, uint64_t post_id, bool saved = true) -> void;
    auto hide_post(uint64_t user_id, uint64_t post_id, bool hidden = true) -> void;
    auto hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden = true) -> void;
    auto hide_board(uint64_t user_id, uint64_t board_id, bool hidden = true) -> void;

    auto create_site_invite(uint64_t inviter_user_id) -> uint64_t;
  };
}
