#include "lmdb_search_engine.h++"
#include "services/search_engine.h++"
#include "util/rich_text.h++"
#include "static/en.wiki.bpe.vs200000.model.h++"

using std::inserter, std::make_shared, std::make_unique, std::optional, std::pair,
    std::runtime_error, std::string, std::string_view, std::vector, phmap::flat_hash_set;

namespace Ludwig {
  static inline auto int_val(uint64_t* i) -> MDB_val {
    return { .mv_size = sizeof(uint64_t), .mv_data = reinterpret_cast<void*>(i) };
  }

  struct LmdbSearchEngine::Txn {
    MDB_txn* txn;
    bool committed = false;
    Txn(MDB_env* env, unsigned flags) {
      mdb_txn_begin(env, nullptr, flags, &txn);
    }
    ~Txn() {
      if (!committed) mdb_txn_abort(txn);
    }
    operator MDB_txn*() { return txn; }
    auto commit() -> int {
      int err = mdb_txn_commit(txn);
      if (err) return err;
      committed = true;
      return 0;
    }
    auto get_all(MDB_dbi dbi, uint64_t key) -> flat_hash_set<uint64_t> {
      flat_hash_set<uint64_t> set;
      MDB_cursor* cur;
      MDB_val k = int_val(&key), v;
      if (mdb_cursor_open(txn, dbi, &cur)) goto done;
      if (mdb_cursor_get(cur, &k, &v, MDB_SET)) goto done;
      while (!mdb_cursor_get(cur, &k, &v, MDB_NEXT_MULTIPLE)) {
        assert(v.mv_size % sizeof(uint64_t) == 0);
        const size_t n = v.mv_size / sizeof(uint64_t);
        for (size_t i = 0; i < n; i++) {
          set.emplace_hint(set.end(), reinterpret_cast<uint64_t*>(v.mv_data)[i]);
        }
      }
    done:
      mdb_cursor_close(cur);
      return set;
    }
    auto del_vals_in_key(MDB_dbi dbi, uint64_t key, flat_hash_set<uint64_t> vals) -> void {
      MDB_cursor* cur;
      MDB_val k = int_val(&key), v;
      if (mdb_cursor_open(txn, dbi, &cur)) goto done;
      for (auto value : vals) {
        v = int_val(&value);
        if (!mdb_cursor_get(cur, &k, &v, MDB_GET_BOTH)) mdb_cursor_del(cur, 0);
      }
    done:
      mdb_cursor_close(cur);
    }
    auto del_val_for_all_keys(MDB_dbi dbi, flat_hash_set<uint64_t> keys, uint64_t value) -> void {
      MDB_cursor* cur;
      MDB_val k, v = int_val(&value);
      if (mdb_cursor_open(txn, dbi, &cur)) goto done;
      for (auto key : keys) {
        k = int_val(&key);
        if (!mdb_cursor_get(cur, &k, &v, MDB_GET_BOTH)) mdb_cursor_del(cur, 0);
      }
    done:
      mdb_cursor_close(cur);
    }
  };

  constexpr unsigned DBI_FLAGS =
    MDB_CREATE | MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_REVERSEDUP;

  LmdbSearchEngine::LmdbSearchEngine(std::filesystem::path filename, size_t map_size_mb)
    : map_size(map_size_mb * MiB - (map_size_mb * MiB) % (size_t)sysconf(_SC_PAGESIZE)) {
    MDB_txn* txn;
    int err;
    if ((err =
      mdb_env_create(&env) ||
      mdb_env_set_maxdbs(env, 6) ||
      mdb_env_set_mapsize(env, map_size) ||
      mdb_env_open(env, filename.c_str(), MDB_NOSUBDIR | MDB_NOSYNC, 0600) ||
      mdb_txn_begin(env, nullptr, 0, &txn) ||
      mdb_dbi_open(txn, "Id_Tokens", DBI_FLAGS, &Id_Tokens) ||
      mdb_dbi_open(txn, "Token_Users", DBI_FLAGS, &Token_Users) ||
      mdb_dbi_open(txn, "Token_Boards", DBI_FLAGS, &Token_Boards) ||
      mdb_dbi_open(txn, "Token_Threads", DBI_FLAGS, &Token_Threads) ||
      mdb_dbi_open(txn, "Token_Comments", DBI_FLAGS, &Token_Comments) ||
      mdb_txn_commit(txn)
    )) throw runtime_error(fmt::format("Search database initialization failed: {}", mdb_strerror(err)));

    const auto status = processor.LoadFromSerializedProto(en_wiki_bpe_vs200000_model_str());
    if (!status.ok()) {
      throw runtime_error("Search tokenizer initialization failed: " + status.ToString());
    }
  }

  LmdbSearchEngine::~LmdbSearchEngine() {
    mdb_env_close(env);
  }

  template <typename T, typename V> static inline auto into_set(flat_hash_set<T>& to, const V& from) -> void {
    std::copy(from.begin(), from.end(), inserter(to, to.end()));
  }

  auto LmdbSearchEngine::index_tokens(Txn&& txn, uint64_t id, MDB_dbi dbi, flat_hash_set<uint64_t>& tokens) -> void {
    flat_hash_set<uint64_t> existing = txn.get_all(Id_Tokens, id);
    MDB_val id_val = int_val(&id), token_val;
    if (!existing.empty()) {
      flat_hash_set<uint64_t> to_insert, to_remove;
      set_difference(tokens.begin(), tokens.end(), existing.begin(), existing.end(), inserter(to_insert, to_insert.begin()));
      set_difference(existing.begin(), existing.end(), tokens.begin(), tokens.end(), inserter(to_remove, to_remove.begin()));
      txn.del_vals_in_key(Id_Tokens, id, to_remove);
      txn.del_val_for_all_keys(dbi, to_remove, id);
      to_insert.swap(tokens);
    }
    for (auto token : tokens) {
      token_val = int_val(&token);
      if (auto err = mdb_put(txn, Id_Tokens, &id_val, &token_val, 0)) {
        spdlog::warn("Search database write failed: {}", mdb_strerror(err));
      }
      if (auto err = mdb_put(txn, dbi, &token_val, &id_val, 0)) {
        spdlog::warn("Search database write failed: {}", mdb_strerror(err));
      }
    }
    txn.commit();
  }

  auto LmdbSearchEngine::index(uint64_t id, const User& user) -> void {
    Txn txn(env, 0);
    flat_hash_set<uint64_t> tokens;
    into_set(tokens, processor.EncodeAsIds(user.name()->string_view()));
    if (user.display_name_type() && user.display_name_type()->size()) {
      into_set(tokens, processor.EncodeAsIds(rich_text_to_plain_text(user.display_name_type(), user.display_name())));
    }
    if (user.bio_type() && user.bio_type()->size()) {
      into_set(tokens, processor.EncodeAsIds(rich_text_to_plain_text(user.bio_type(), user.bio())));
    }
    index_tokens(std::move(txn), id, Token_Users, tokens);
  }

  auto LmdbSearchEngine::index(uint64_t id, const Board& board) -> void {
    Txn txn(env, 0);
    flat_hash_set<uint64_t> tokens;
    into_set(tokens, processor.EncodeAsIds(board.name()->string_view()));
    if (board.display_name_type() && board.display_name_type()->size()) {
      into_set(tokens, processor.EncodeAsIds(rich_text_to_plain_text(board.display_name_type(), board.display_name())));
    }
    if (board.description_type() && board.description_type()->size()) {
      into_set(tokens, processor.EncodeAsIds(rich_text_to_plain_text(board.description_type(), board.description())));
    }
    index_tokens(std::move(txn), id, Token_Boards, tokens);
  }

  auto LmdbSearchEngine::index(uint64_t id, const Thread& thread, optional<std::reference_wrapper<const LinkCard>> card_opt) -> void {
    Txn txn(env, 0);
    flat_hash_set<uint64_t> tokens;
    into_set(tokens, processor.EncodeAsIds(rich_text_to_plain_text(thread.title_type(), thread.title())));
    if (thread.content_text_type() && thread.content_text_type()->size()) {
      into_set(tokens, processor.EncodeAsIds(rich_text_to_plain_text(thread.content_text_type(), thread.content_text())));
    }
    if (card_opt) {
      const auto& card = card_opt->get();
      if (card.title()) {
        into_set(tokens, processor.EncodeAsIds(card.title()->string_view()));
      }
      if (card.description()) {
        into_set(tokens, processor.EncodeAsIds(card.description()->string_view()));
      }
    }
    index_tokens(std::move(txn), id, Token_Threads, tokens);
  }

  auto LmdbSearchEngine::index(uint64_t id, const Comment& comment) -> void {
    Txn txn(env, 0);
    flat_hash_set<uint64_t> tokens;
    into_set(tokens, processor.EncodeAsIds(rich_text_to_plain_text(comment.content_type(), comment.content())));
    index_tokens(std::move(txn), id, Token_Comments, tokens);
  }

  auto LmdbSearchEngine::unindex(uint64_t id, SearchResultType type) -> void {
    using enum SearchResultType;
    Txn txn(env, 0);
    auto tokens = txn.get_all(Id_Tokens, id);
    MDB_val id_val = int_val(&id);
    if (auto err = mdb_del(txn, Id_Tokens, &id_val, nullptr)) {
      spdlog::warn("Search database delete failed: {}", mdb_strerror(err));
    }
    MDB_dbi dbi;
    switch (type) {
      case User: dbi = Token_Users; break;
      case Board: dbi = Token_Boards; break;
      case Thread: dbi = Token_Threads; break;
      case Comment: dbi = Token_Comments; break;
    }
    txn.del_val_for_all_keys(dbi, tokens, id);
    txn.commit();
  }

  using MatchMap = phmap::flat_hash_map<uint64_t, pair<SearchResultType, uint64_t>>;

  static inline auto into_match_map(MatchMap& mm, SearchResultType type, flat_hash_set<uint64_t> ids) {
    for (auto id : ids) {
      const auto [it, inserted] = mm.insert({ id, { type, 1 } });
      if (!inserted) it->second.second++;
    }
  }

  auto LmdbSearchEngine::search(SearchQuery query) -> std::shared_ptr<CompletableOnce<vector<SearchResult>>> {
    using enum SearchResultType;
    flat_hash_set<int> tokens;
    MatchMap matches;

    // string-start tokens are different from mid-string tokens
    into_set(tokens, processor.EncodeAsIds(query.query));
    into_set(tokens, processor.EncodeAsIds(" " + string(query.query)));

    Txn txn(env, MDB_RDONLY);
    for (const auto token : tokens) {
      if (query.include_users) into_match_map(matches, User, txn.get_all(Token_Users, (uint64_t)token));
      if (query.include_boards) into_match_map(matches, Board, txn.get_all(Token_Boards, (uint64_t)token));
      if (query.include_threads) into_match_map(matches, Thread, txn.get_all(Token_Threads, (uint64_t)token));
      if (query.include_comments) into_match_map(matches, Comment, txn.get_all(Token_Comments, (uint64_t)token));
    }
    if (matches.size() <= query.offset) {
      return make_shared<CompletableOnce<vector<SearchResult>>>(vector<SearchResult>{});
    }

    // TODO: Filter and sort

    auto buf = make_unique<pair<uint64_t, pair<SearchResultType, uint64_t>>[]>(query.offset + query.limit);
    std::partial_sort_copy(matches.begin(), matches.end(), buf.get(), buf.get() + query.offset + query.limit, [](auto a, auto b) {
      return a.second.second > b.second.second;
    });
    const auto n = std::min(matches.size() - query.offset, query.limit);
    vector<SearchResult> results;
    results.reserve(n);
    for (size_t i = 0; i < n; i++) {
      results.emplace_back(buf[query.offset + i].second.first, buf[query.offset + i].first);
    }

    return make_shared<CompletableOnce<vector<SearchResult>>>(std::move(results));
  }
}
