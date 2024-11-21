#pragma once
#include "util/common.h++"
#include "fbs/records.h++"

namespace Ludwig {
  enum class SearchResultType : uint8_t {
    User, Board, Thread, Comment
  };

  enum class SearchResultSort : uint8_t {
    Relevant, Top, New
  };

  struct SearchQuery {
    std::string_view query;
    bool include_users, include_boards, include_threads, include_comments, include_cws;
    SearchResultSort sort;
    uint64_t board_id;
    size_t offset, limit;
  };

  struct SearchResult {
    SearchResultType type;
    uint64_t id;
  };

  class SearchEngine {
  public:
    virtual ~SearchEngine() = default;
    virtual auto index( uint64_t id, const User& user) -> void = 0;
    virtual auto index(uint64_t id, const Board& board) -> void = 0;
    virtual auto index(uint64_t id, const Thread& thread, std::optional<std::reference_wrapper<const LinkCard>> card_opt = {}) -> void = 0;
    virtual auto index(uint64_t id, const Comment& comment) -> void = 0;
    virtual auto unindex(uint64_t id, SearchResultType type) -> void = 0;
    virtual auto search(SearchQuery query) -> std::shared_ptr<CompletableOnce<std::vector<SearchResult>>> = 0;
  };
}
