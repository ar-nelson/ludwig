#pragma once
#include "services/search_engine.h++"
#include <filesystem>
#include <lmdb.h>
#include <sentencepiece_processor.h>

namespace Ludwig {
  class LmdbSearchEngine : public SearchEngine {
  private:
    size_t map_size;
    MDB_env* env;
    MDB_dbi Id_Tokens, Token_Users, Token_Boards, Token_Threads, Token_Comments;
    sentencepiece::SentencePieceProcessor processor;
    struct Txn;
    auto index_tokens(Txn&& txn, uint64_t id, MDB_dbi dbi, std::set<uint64_t> tokens) -> void;
  public:
    LmdbSearchEngine(std::filesystem::path filename, size_t map_size_mb = 1024);
    ~LmdbSearchEngine();
    auto index(uint64_t id, const User& user) -> void;
    auto index(uint64_t id, const Board& board) -> void;
    auto index(uint64_t id, const Thread& thread, std::optional<std::reference_wrapper<const LinkCard>> card_opt) -> void;
    auto index(uint64_t id, const Comment& comment) -> void;
    auto unindex(uint64_t id, SearchResultType type) -> void;
    auto search(SearchQuery query, Callback&& callback) -> void;
  };
}
