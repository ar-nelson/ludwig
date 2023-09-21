#pragma once
#include <bitset>
#include <map>
#include <shared_mutex>
#include <asio.hpp>
#include <monocypher.h>
#include "generated/datatypes_generated.h"
#include "db.h++"

namespace Ludwig {
  constexpr size_t ITEMS_PER_PAGE = 20;

  enum class SortType : uint8_t {
    Active,
    Hot,
    New,
    Old,
    MostComments,
    NewComments,
    TopAll,
    TopYear,
    TopSixMonths,
    TopThreeMonths,
    TopMonth,
    TopWeek,
    TopDay,
    TopTwelveHour,
    TopSixHour,
    TopHour
  };

  enum class CommentSortType : uint8_t {
    Hot,
    Top,
    New,
    Old
  };

  enum class UserPostSortType : uint8_t {
    Top,
    New,
    Old
  };

  struct SecretString {
    std::string_view str;
    SecretString(std::string_view str) : str(str) {};
    SecretString(const SecretString&) = delete;
    SecretString& operator=(const SecretString&) = delete;
    ~SecretString() {
      crypto_wipe((char*)str.data(), str.length());
    }
  };

  struct LoginResponse {
    uint64_t user_id, session_id, expiration;
  };

  struct SiteDetail {
    std::string name, domain, description;
    std::optional<std::string> icon_url, banner_url;
  };

  struct UserDetailResponse {
    uint64_t id;
    const User* user;
    const UserStats* stats;
  };

  struct LocalUserDetailResponse {
    uint64_t id;
    const LocalUser* local_user;
    const User* user;
    const UserStats* stats;
  };

  struct BoardDetailResponse {
    uint64_t id;
    const Board* board;
    const BoardStats* stats;
  };

  struct LocalBoardDetailResponse {
    uint64_t id;
    const LocalBoard* local_board;
    const Board* board;
    const BoardStats* stats;
  };

  struct UserListEntry {
    uint64_t id;
    const User* user;
  };

  struct BoardListEntry {
    uint64_t id;
    const Board* board;
  };

  struct PageListEntry {
    uint64_t id;
    double rank;
    Vote your_vote;
    const Page* page;
    const PostStats* stats;
    const User* author;
    const Board* board;
  };

  struct NoteListEntry {
    uint64_t id;
    double rank;
    Vote your_vote;
    const Note* note;
    const PostStats* stats;
    const User* author;
    const Page* page;
  };

  struct ListUsersResponse {
    std::array<UserListEntry, ITEMS_PER_PAGE> page;
    size_t size;
    std::optional<uint64_t> next;
  };

  struct ListBoardsResponse {
    std::array<BoardListEntry, ITEMS_PER_PAGE> page;
    size_t size;
    std::optional<uint64_t> next;
  };

  struct ListPagesResponse {
    std::array<PageListEntry, ITEMS_PER_PAGE> page;
    size_t size;
    std::optional<uint64_t> next;
  };

  struct ListNotesResponse {
    std::array<NoteListEntry, ITEMS_PER_PAGE> page;
    size_t size;
    std::optional<uint64_t> next;
  };

  struct CommentTree {
    std::unordered_map<uint64_t, uint64_t> continued;
    std::multimap<uint64_t, NoteListEntry> notes;

    inline auto size() -> size_t {
      return notes.size();
    }
    inline auto emplace(uint64_t parent, NoteListEntry e) -> void {
      notes.emplace(parent, e);
    }
    inline auto mark_continued(uint64_t parent, uint64_t from = 0) {
      continued.emplace(parent, from);
    }
  };

  struct PageDetailResponse {
    uint64_t id;
    Vote your_vote;
    const Page* page;
    const PostStats* stats;
    const User* author;
    const Board* board;
    CommentTree comments;
  };

  struct NoteDetailResponse {
    uint64_t id;
    Vote your_vote;
    const Note* note;
    const PostStats* stats;
    const User* author;
    const Page* page;
    CommentTree comments;
  };

  class ControllerError : public std::runtime_error {
    private:
      uint16_t _http_error;
    public:
      ControllerError(
        const char* message,
        uint16_t http_error = 500
      ) : std::runtime_error(message), _http_error(http_error) {}
      inline auto http_error() const -> uint16_t {
        return _http_error;
      }
  };

  class Controller;

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
    PageUpdate,
    PageStatsUpdate,
    PageDelete,
    NoteUpdate,
    NoteStatsUpdate,
    NoteDelete,
    MaxEvent
  };

  typedef std::function<auto (Event, uint64_t) -> void> EventCallback;

  struct EventListener {
    uint64_t id, subject_id;
    Event event;
    EventCallback callback;

    EventListener(
      uint64_t id,
      Event event,
      uint64_t subject_id,
      EventCallback&& callback
    ) : id(id), subject_id(subject_id), event(event), callback(std::move(callback)) {}

    void operator()() {
      callback(event, subject_id);
    }
  };

  class EventSubscription {
  private:
    std::weak_ptr<Controller> controller;
    uint64_t id;
    std::pair<Event, uint64_t> key;
  public:
    EventSubscription(
      std::shared_ptr<Controller> controller,
      uint64_t id,
      Event event,
      uint64_t subject_id
    ) : controller(controller), id(id), key(event, subject_id) {}
    ~EventSubscription();
  };

  class Controller : std::enable_shared_from_this<Controller> {
  private:
    std::shared_ptr<DB> db;
    SiteDetail cached_site_detail;

    std::shared_ptr<asio::io_context> io;
    asio::executor_work_guard<decltype(io->get_executor())> work;
    std::shared_mutex listener_lock;
    uint64_t next_event_id = 0;
    std::multimap<std::pair<Event, uint64_t>, EventListener> event_listeners;

    auto dispatch_event(Event event, uint64_t subject_id = 0) -> void;
  public:
    Controller(std::shared_ptr<DB> db, std::shared_ptr<asio::io_context> io);

    static auto parse_sort_type(std::string_view str) -> SortType {
      if (str.empty() || str == "Hot") return SortType::Hot;
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
      throw ControllerError("Bad sort type", 400);
    }

    static auto parse_comment_sort_type(std::string_view str) -> CommentSortType {
      if (str.empty() || str == "Hot") return CommentSortType::Hot;
      if (str == "New") return CommentSortType::New;
      if (str == "Old") return CommentSortType::Old;
      if (str == "Top") return CommentSortType::Top;
      throw ControllerError("Bad comment sort type", 400);
    }

    static auto parse_user_post_sort_type(std::string_view str) -> UserPostSortType {
      if (str.empty() || str == "New") return UserPostSortType::New;
      if (str == "Old") return UserPostSortType::Old;
      if (str == "Top") return UserPostSortType::Top;
      throw ControllerError("Bad post sort type", 400);
    }

    static auto parse_hex_id(std::string hex_id) -> std::optional<uint64_t> {
      if (hex_id.empty()) return {};
      auto n = std::stoull(hex_id, nullptr, 16);
      if (!n && hex_id != "0") throw ControllerError("Bad hexadecimal ID", 400);
      return { n };
    }

    inline auto open_read_txn() -> ReadTxn {
      return db->open_read_txn();
    }

    inline auto validate_session(ReadTxnBase& txn, uint64_t session_id) -> std::optional<uint64_t> {
      return txn.validate_session(session_id);
    }

    auto hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[32]) -> void;

    auto login(
      std::string_view username,
      SecretString&& password,
      string_view ip,
      string_view user_agent
    ) -> LoginResponse;
    inline auto site_detail() -> const SiteDetail* {
      return &cached_site_detail;
    }
    auto page_detail(
      ReadTxnBase& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      bool skip_cw = false,
      uint64_t viewer_user = 0,
      std::optional<uint64_t> from_id = {}
    ) -> PageDetailResponse;
    auto note_detail(
      ReadTxnBase& txn,
      uint64_t id,
      CommentSortType sort = CommentSortType::Hot,
      bool skip_cw = false,
      uint64_t viewer_user = 0,
      std::optional<uint64_t> from_id = {}
    ) -> NoteDetailResponse;
    auto user_detail(ReadTxnBase& txn, uint64_t id) -> UserDetailResponse;
    auto local_user_detail(ReadTxnBase& txn, uint64_t id) -> LocalUserDetailResponse;
    auto board_detail(ReadTxnBase& txn, uint64_t id) -> BoardDetailResponse;
    auto list_local_users(ReadTxnBase& txn, std::optional<uint64_t> from_id = {}) -> ListUsersResponse;
    auto list_local_boards(ReadTxnBase& txn, std::optional<uint64_t> from_id = {}) -> ListBoardsResponse;
    auto list_board_pages(
      ReadTxnBase& txn,
      uint64_t board_id,
      SortType sort = SortType::Hot,
      bool skip_cw = false,
      uint64_t viewer_user = 0,
      std::optional<uint64_t> from_id = {}
    ) -> ListPagesResponse;
    auto list_board_notes(
      ReadTxnBase& txn,
      uint64_t board_id,
      SortType sort = SortType::Hot,
      bool skip_cw = false,
      uint64_t viewer_user = 0,
      std::optional<uint64_t> from_id = {}
    ) -> ListNotesResponse;
    auto list_user_pages(
      ReadTxnBase& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      bool skip_cw = false,
      uint64_t viewer_user = 0,
      std::optional<uint64_t> from_id = {}
    ) -> ListPagesResponse;
    auto list_user_notes(
      ReadTxnBase& txn,
      uint64_t user_id,
      UserPostSortType sort = UserPostSortType::New,
      bool skip_cw = false,
      uint64_t viewer_user = 0,
      std::optional<uint64_t> from_id = {}
    ) -> ListNotesResponse;
    auto create_local_user(
      std::string_view username,
      std::string_view email,
      SecretString&& password
    ) -> uint64_t;
    auto create_local_board(
      uint64_t owner,
      std::string_view name,
      std::optional<std::string_view> display_name,
      std::optional<std::string_view> content_warning = {},
      bool is_private = false,
      bool is_restricted_posting = false,
      bool is_local_only = false
    ) -> uint64_t;
    auto create_local_page(
      uint64_t author,
      uint64_t board,
      std::string_view title,
      std::optional<std::string_view> submission_url,
      std::optional<std::string_view> text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto create_local_note(
      uint64_t author,
      uint64_t parent,
      std::string_view text_content_markdown,
      std::optional<std::string_view> content_warning = {}
    ) -> uint64_t;
    auto vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void;
    auto subscribe(uint64_t user_id, uint64_t board_id, bool subscribed = true) -> void;

    auto on_event(Event event, uint64_t subject_id, EventCallback&& callback) -> EventSubscription;

    friend class EventSubscription;
  };
}
