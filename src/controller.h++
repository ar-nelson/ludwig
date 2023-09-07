#pragma once
#include <monocypher.h>
#include "generated/datatypes_generated.h"
#include "db.h++"

namespace Ludwig {
  constexpr size_t PAGE_SIZE = 20;

  enum class SortType {
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

  enum class CommentSortType {
    Hot,
    Top,
    New,
    Old
  };

  enum class UserPostSortType {
    Top,
    New,
    Old
  };

  struct SecretString {
    std::string&& str;
    ~SecretString() {
      crypto_wipe(str.data(), str.length());
    }
  };

  struct LoginResponse {
    uint64_t user_id;
    SecretString jwt;
  };

  struct SiteDetail {
    std::string name, domain, description;
    std::optional<std::string> icon_url, banner_url;
  };

  struct PageDetailResponse {
    ReadTxn&& txn;
    const Page* page;
    const PageStats* stats;
  };

  struct UserDetailResponse {
    ReadTxn&& txn;
    const User* user;
    const UserStats* stats;
  };

  struct BoardDetailResponse {
    ReadTxn&& txn;
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
    const Page* page;
    const PageStats* stats;
  };

  struct NoteListEntry {
    uint64_t id;
    double rank;
    const Note* note;
    const NoteStats* stats;
  };

  struct ListUsersResponse {
    ReadTxn&& txn;
    std::array<UserListEntry, PAGE_SIZE> page;
    size_t size;
    std::optional<uint64_t> next;
  };

  struct ListBoardsResponse {
    ReadTxn&& txn;
    std::array<BoardListEntry, PAGE_SIZE> page;
    size_t size;
    std::optional<uint64_t> next;
  };

  struct ListPagesResponse {
    ReadTxn&& txn;
    std::array<PageListEntry, PAGE_SIZE> page;
    size_t size;
    std::optional<uint64_t> next;
  };

  struct ListNotesResponse {
    ReadTxn&& txn;
    std::array<NoteListEntry, PAGE_SIZE> page;
    size_t size;
    std::optional<uint64_t> next;
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

  class Controller {
  private:
    DB& db;
    SiteDetail cached_site_detail;
    auto hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[64]) -> void;
  public:
    Controller(DB& db);
    auto get_auth_user(SecretString&& jwt) -> uint64_t;
    auto login(std::string_view username, SecretString&& password) -> LoginResponse;
    inline auto site_detail() -> const SiteDetail* {
      return &cached_site_detail;
    }
    auto page_detail(uint64_t id) -> PageDetailResponse;
    auto user_detail(uint64_t id) -> UserDetailResponse;
    auto board_detail(uint64_t id) -> BoardDetailResponse;
    auto list_local_users(uint64_t from_id = 0) -> ListUsersResponse;
    auto list_local_boards(uint64_t from_id = 0) -> ListBoardsResponse;
    auto list_board_pages(uint64_t board_id, SortType sort = SortType::Hot, uint64_t from_id = 0) -> ListPagesResponse;
    auto list_board_notes(uint64_t board_id, CommentSortType sort = CommentSortType::Hot, uint64_t from_id = 0) -> ListNotesResponse;
    auto list_child_notes(uint64_t parent_id, CommentSortType sort = CommentSortType::Hot, uint64_t from_id = 0) -> ListNotesResponse;
    auto list_user_pages(uint64_t user_id, UserPostSortType sort = UserPostSortType::New, uint64_t from_id = 0) -> ListPagesResponse;
    auto list_user_notes(uint64_t user_id, UserPostSortType sort = UserPostSortType::New, uint64_t from_id = 0) -> ListNotesResponse;
    auto create_local_user() -> uint64_t;
    auto create_local_board() -> uint64_t;
    auto create_local_page() -> uint64_t;
    auto create_local_note() -> uint64_t;
    auto vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void;
    auto subscribe(uint64_t user_id, uint64_t board_id) -> void;
    auto unsubscribe(uint64_t user_id, uint64_t board_id) -> void;
  };
}
