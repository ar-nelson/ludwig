#include <random>
#include <regex>
#include <span>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <duthomhas/csprng.hpp>
#include <flatbuffers/idl.h>
#include "generated/datatypes.fbs.h"
#include "db.h++"

using std::min, std::nullopt, std::regex, std::regex_match, std::runtime_error,
      std::optional, std::stoull, std::string, std::string_view,
      flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot;

#define assert_fmt(CONDITION, ...) if (!(CONDITION)) { spdlog::critical(__VA_ARGS__); throw runtime_error("Assertion failed: " #CONDITION); }

namespace Ludwig {
  static const regex
    json_line(R"(^([uUbBtcdarm]) (\d+) (\{[^\n]+\})$)", regex::ECMAScript),
    setting_line(R"(^S (\w+) ([0-9a-zA-Z+/]+=*)$)", regex::ECMAScript),
    vote_line(R"(^v (\d+) (\d+) (1|-1)$)", regex::ECMAScript),
    subscription_line(R"(^s (\d+) (\d+) (\d+)$)", regex::ECMAScript);

  static constexpr uint64_t ACTIVE_COMMENT_MAX_AGE = 86400 * 2; // 2 days

  enum Dbi {
    Settings,
    Session_Session,

    User_User,
    User_Name,
    User_Email,
    UserStats_User,
    LocalUser_User,
    Application_User,
    Owner_UserInvite,
    Owner_UserBoard,
    Owner_UserThread,
    Owner_UserComment,
    Vote_UserPost,
    ThreadsTop_UserKarmaThread,
    CommentsTop_UserKarmaComment,
    Save_UserPost,
    Hide_UserPost,
    Hide_UserUser,
    Hide_UserBoard,
    Subscription_UserBoard,
    Owner_UserMedia,

    Board_Board,
    Board_Name,
    BoardStats_Board,
    LocalBoard_Board,
    ThreadsTop_BoardKarmaThread,
    ThreadsNew_BoardTimeThread,
    ThreadsMostComments_BoardCommentsThread,
    CommentsTop_BoardKarmaComment,
    CommentsNew_BoardTimeComment,
    CommentsMostComments_BoardCommentsComment,
    Subscription_BoardUser,

    Thread_Thread,
    Comment_Comment,
    PostStats_Post,
    ChildrenNew_PostTimeComment,
    ChildrenTop_PostKarmaComment,
    Contains_PostMedia,

    Invite_Invite,
    Media_Media,
    Contains_MediaPost,
  };

  static inline auto db_get(MDB_txn* txn, MDB_dbi dbi, MDB_val& k, MDB_val& v) -> int {
    return mdb_get(txn, dbi, &k, &v);
  }

  static inline auto db_get(MDB_txn* txn, MDB_dbi dbi, string_view k, MDB_val& v) -> int {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    return mdb_get(txn, dbi, &kval, &v);
  }

  static inline auto db_get(MDB_txn* txn, MDB_dbi dbi, uint64_t k, MDB_val& v) -> int {
    MDB_val kval { sizeof(uint64_t), &k };
    return mdb_get(txn, dbi, &kval, &v);
  }

  static inline auto db_get(MDB_txn* txn, MDB_dbi dbi, Cursor k, MDB_val& v) -> int {
    MDB_val kval = k.val();
    return mdb_get(txn, dbi, &kval, &v);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, MDB_val& k, MDB_val& v, unsigned flags = 0) -> void {
    if (auto err = mdb_put(txn, dbi, &k, &v, flags)) throw DBError("Write failed", err);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, MDB_val& v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    db_put(txn, dbi, kval, v, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, string_view v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    MDB_val vval { v.length(), const_cast<char*>(v.data()) };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, uint64_t v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    MDB_val vval { sizeof(uint64_t), &v };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v, unsigned flags = 0) -> void {
    MDB_val kval = k.val();
    MDB_val vval { sizeof(uint64_t), &v };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, uint64_t k, FlatBufferBuilder& fbb, unsigned flags = 0) -> void {
    MDB_val kval { sizeof(uint64_t), &k };
    MDB_val vval { fbb.GetSize(), fbb.GetBufferPointer() };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, Cursor k, MDB_val& v, unsigned flags = 0) -> void {
    MDB_val kval = k.val();
    db_put(txn, dbi, kval, v, flags);
  }

  static inline auto db_del(MDB_txn* txn, MDB_dbi dbi, Cursor k) -> void {
    MDB_val kval = k.val();
    if (auto err = mdb_del(txn, dbi, &kval, nullptr)) {
      if (err != MDB_NOTFOUND) throw DBError("Delete failed", err);
    }
  }

  static inline auto db_del(MDB_txn* txn, MDB_dbi dbi, uint64_t k) -> void {
    MDB_val kval = { sizeof(uint64_t), &k };
    if (auto err = mdb_del(txn, dbi, &kval, nullptr)) {
      if (err != MDB_NOTFOUND) throw DBError("Delete failed", err);
    }
  }

  static auto db_set_has(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v) -> bool {
    MDB_val val;
    if (db_get(txn, dbi, k, val)) return false;
    assert(val.mv_size % sizeof(uint64_t) == 0);
    const uint64_t* ptr = reinterpret_cast<uint64_t*>(val.mv_data);
    if (val.mv_size == sizeof(uint64_t)) return v == *ptr;
    const std::span sp(ptr, val.mv_size / sizeof(uint64_t));
    return std::binary_search(sp.begin(), sp.end(), v);
  }

  static inline auto db_set_disambiguate_hash(MDB_txn* txn, MDB_dbi dbi, Cursor k, std::function<bool (uint64_t)> matches) -> optional<uint64_t> {
    MDB_val val;
    if (db_get(txn, dbi, k, val)) return false;
    assert(val.mv_size % sizeof(uint64_t) == 0);
    const uint64_t* ptr = reinterpret_cast<uint64_t*>(val.mv_data);
    if (val.mv_size == sizeof(uint64_t)) return val_as<uint64_t>(val);
    const std::span sp(ptr, val.mv_size / sizeof(uint64_t));
    for (uint64_t id : sp) {
      if (matches(id)) return id;
    }
    return {};
  }

  static auto db_set_put(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v) -> void {
    MDB_val val;
    if (int err = db_get(txn, dbi, k, val)) {
      if (err != MDB_NOTFOUND) throw DBError("Read failed", err);
      db_put(txn, dbi, k, v);
      return;
    }
    assert(val.mv_size % sizeof(uint64_t) == 0);
    const uint64_t* ptr = reinterpret_cast<uint64_t*>(val.mv_data);
    std::vector vec(ptr, ptr + (val.mv_size / sizeof(uint64_t)));
    const auto insert_at = std::lower_bound(vec.begin(), vec.end(), v);
    if (insert_at != vec.end() && *insert_at == v) return;
    vec.insert(insert_at, v);
    val.mv_size = vec.size() * sizeof(uint64_t);
    val.mv_data = &vec[0];
    db_put(txn, dbi, k, val);
  }

  static auto db_set_del(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v) -> void {
    MDB_val val;
    if (db_get(txn, dbi, k, val)) return;
    assert(val.mv_size % sizeof(uint64_t) == 0);
    const uint64_t* ptr = reinterpret_cast<uint64_t*>(val.mv_data);
    if (val.mv_size == sizeof(uint64_t)) {
      if (v == *ptr) db_del(txn, dbi, k);
      return;
    }
    std::vector vec(ptr, ptr + (val.mv_size / sizeof(uint64_t)));
    const auto remove_at = std::lower_bound(vec.begin(), vec.end(), v);
    if (remove_at == vec.end() || *remove_at != v) return;
    vec.erase(remove_at);
    val.mv_size = vec.size() * sizeof(uint64_t);
    val.mv_data = &vec[0];
    db_put(txn, dbi, k, val);
  }

  struct MDBCursor {
    MDB_cursor* cur;
    MDBCursor(MDB_txn* txn, MDB_dbi dbi) {
      if (auto err = mdb_cursor_open(txn, dbi, &cur)) {
        throw DBError("Failed to open database cursor", err);
      }
    }
    ~MDBCursor() {
      mdb_cursor_close(cur);
    }
    inline operator MDB_cursor*() {
      return cur;
    }
  };

  auto DB::init_env(const char* filename, MDB_txn** txn) -> int {
    int err;
    if ((err =
      mdb_env_create(&env) ||
      mdb_env_set_maxdbs(env, 128) ||
      mdb_env_set_mapsize(env, map_size) ||
      mdb_env_open(env, filename, MDB_NOSUBDIR | MDB_NOSYNC, 0600) ||
      mdb_txn_begin(env, nullptr, 0, txn)
    )) return err;

#   define MK_DBI(NAME, FLAGS) if ((err = mdb_dbi_open(*txn, #NAME, FLAGS | MDB_CREATE, dbis + NAME))) return err;
    MK_DBI(Settings, 0)
    MK_DBI(Session_Session, MDB_INTEGERKEY)

    MK_DBI(User_User, MDB_INTEGERKEY)
    MK_DBI(User_Name, MDB_INTEGERKEY)
    MK_DBI(User_Email, MDB_INTEGERKEY)
    MK_DBI(UserStats_User, MDB_INTEGERKEY)
    MK_DBI(LocalUser_User, MDB_INTEGERKEY)
    MK_DBI(Application_User, MDB_INTEGERKEY)
    MK_DBI(Owner_UserInvite, 0)
    MK_DBI(Owner_UserBoard, 0)
    MK_DBI(Owner_UserThread, 0)
    MK_DBI(Owner_UserComment, 0)
    MK_DBI(ThreadsTop_UserKarmaThread, 0)
    MK_DBI(CommentsTop_UserKarmaComment, 0)
    MK_DBI(Save_UserPost, 0)
    MK_DBI(Hide_UserPost, 0)
    MK_DBI(Hide_UserUser, 0)
    MK_DBI(Hide_UserBoard, 0)
    MK_DBI(Subscription_UserBoard, 0)
    MK_DBI(Owner_UserMedia, 0)
    MK_DBI(Vote_UserPost, 0)

    MK_DBI(Board_Board, MDB_INTEGERKEY)
    MK_DBI(Board_Name, MDB_INTEGERKEY)
    MK_DBI(BoardStats_Board, MDB_INTEGERKEY)
    MK_DBI(LocalBoard_Board, MDB_INTEGERKEY)
    MK_DBI(ThreadsTop_BoardKarmaThread, 0)
    MK_DBI(ThreadsNew_BoardTimeThread, 0)
    MK_DBI(ThreadsMostComments_BoardCommentsThread, 0)
    MK_DBI(CommentsTop_BoardKarmaComment, 0)
    MK_DBI(CommentsNew_BoardTimeComment, 0)
    MK_DBI(CommentsMostComments_BoardCommentsComment, 0)
    MK_DBI(Subscription_BoardUser, 0)

    MK_DBI(Thread_Thread, MDB_INTEGERKEY)
    MK_DBI(Comment_Comment, MDB_INTEGERKEY)
    MK_DBI(PostStats_Post, MDB_INTEGERKEY)
    MK_DBI(ChildrenNew_PostTimeComment, 0)
    MK_DBI(ChildrenTop_PostKarmaComment, 0)
    MK_DBI(Contains_PostMedia, 0)

    MK_DBI(Invite_Invite, MDB_INTEGERKEY)
    MK_DBI(Media_Media, MDB_INTEGERKEY)
    MK_DBI(Contains_MediaPost, 0)
#   undef MK_DBI

    return 0;
  }

  DB::DB(const char* filename, size_t map_size_mb) :
    map_size(map_size_mb * 1024 * 1024 - (map_size_mb * 1024 * 2014) % (size_t)sysconf(_SC_PAGESIZE)) {
    MDB_txn* txn = nullptr;
    int err = init_env(filename, &txn);
    if (err) goto die;

    // Load the secrets, or generate them if missing
    MDB_val val;
    if ((err = db_get(txn, dbis[Settings], SettingsKey::hash_seed, val))) {
      spdlog::info("Opened database {} for the first time, generating secrets", filename);
      duthomhas::csprng rng;
      rng(seed);
      rng(jwt_secret);
      const auto now = now_s();
      db_put(txn, dbis[Settings], SettingsKey::next_id, 1ULL);
      db_put(txn, dbis[Settings], SettingsKey::hash_seed, seed);
      val = { JWT_SECRET_SIZE, jwt_secret };
      db_put(txn, dbis[Settings], SettingsKey::jwt_secret, val);
      db_put(txn, dbis[Settings], SettingsKey::domain, "http://localhost:2023");
      db_put(txn, dbis[Settings], SettingsKey::created_at, now);
      db_put(txn, dbis[Settings], SettingsKey::updated_at, now);
      db_put(txn, dbis[Settings], SettingsKey::name, "Ludwig");
      db_put(txn, dbis[Settings], SettingsKey::description, "A new Ludwig server");
      db_put(txn, dbis[Settings], SettingsKey::post_max_length, 1024 * 1024);
      db_put(txn, dbis[Settings], SettingsKey::media_upload_enabled, 0ULL);
      db_put(txn, dbis[Settings], SettingsKey::board_creation_admin_only, 1ULL);
      db_put(txn, dbis[Settings], SettingsKey::federation_enabled, 0ULL);
      db_put(txn, dbis[Settings], SettingsKey::federate_cw_content, 1ULL);
    } else {
      spdlog::debug("Loaded existing database {}", filename);
      seed = val_as<uint64_t>(val);
      if ((err = db_get(txn, dbis[Settings], SettingsKey::jwt_secret, val))) goto die;
      assert_fmt(val.mv_size == JWT_SECRET_SIZE, "jwt_secret is wrong size: expected {}, got {}", JWT_SECRET_SIZE, val.mv_size);
      memcpy(jwt_secret, val.mv_data, JWT_SECRET_SIZE);
    }
    if ((err = mdb_txn_commit(txn))) goto die;
    return;
  die:
    if (txn != nullptr) mdb_txn_abort(txn);
    mdb_env_close(env);
    throw DBError("Failed to open database", err);
  }

  struct DeferDelete {
    MDB_env* env;
    const char* filename;
    bool canceled = false;

    ~DeferDelete() {
      if (!canceled) {
        mdb_env_close(env);
        remove(filename);
      }
    }
  };

  DB::DB(const char* filename, std::istream& dump_stream, size_t map_size_mb) :
    map_size(map_size_mb * 1024 * 1024 - (map_size_mb * 1024 * 2014) % (size_t)sysconf(_SC_PAGESIZE)) {
    {
      struct stat stat_buf;
      if (stat(filename, &stat_buf) == 0) {
        throw runtime_error("Cannot import database dump: database file " +
            string(filename) + " already exists and would be overwritten.");
      }
    }

    flatbuffers::Parser parser;
    parser.Parse(
      string(string_view(reinterpret_cast<char*>(datatypes_fbs), datatypes_fbs_len)).c_str(),
      nullptr,
      "datatypes.fbs"
    );

    {
      MDB_txn* txn = nullptr;
      if (int err = init_env(filename, &txn) || mdb_txn_commit(txn)) {
        if (txn != nullptr) mdb_txn_abort(txn);
        mdb_env_close(env);
        remove(filename);
        throw DBError("Failed to open database", err);
      }
    }

    DeferDelete on_error{ env, filename };
    auto txn = open_write_txn();
    string line;
    seed = 0;
    while (std::getline(dump_stream, line)) {
      if (line.empty()) continue;
      std::smatch match;
      if (regex_match(line, match, json_line)) {
        switch (match[1].str()[0]) {
          case 'u':
            if (seed == 0) {
              spdlog::warn("hash_seed was not set before creating users, name lookup may be broken");
            }
            parser.SetRootType("User");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw runtime_error("Failed to parse User JSON: " + match[3].str());
            }
            txn.set_user(std::stoull(match[2]), parser.builder_);
            break;
          case 'U':
            parser.SetRootType("LocalUser");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw runtime_error("Failed to parse LocalUser JSON: " + match[3].str());
            }
            txn.set_local_user(std::stoull(match[2]), parser.builder_);
            break;
          case 'b':
            if (seed == 0) {
              spdlog::warn("hash_seed was not set before creating boards, name lookup may be broken");
            }
            parser.SetRootType("Board");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw runtime_error("Failed to parse Board JSON: " + match[3].str());
            }
            txn.set_board(std::stoull(match[2]), parser.builder_);
            break;
          case 'B':
            parser.SetRootType("LocalBoard");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw runtime_error("Failed to parse LocalBoard JSON: " + match[3].str());
            }
            txn.set_local_board(std::stoull(match[2]), parser.builder_);
            break;
          case 't':
            parser.SetRootType("Thread");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw runtime_error("Failed to parse Thread JSON: " + match[3].str());
            }
            txn.set_thread(std::stoull(match[2]), parser.builder_);
            break;
          case 'c':
            parser.SetRootType("Comment");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw runtime_error("Failed to parse Comment JSON: " + match[3].str());
            }
            txn.set_comment(std::stoull(match[2]), parser.builder_);
            break;
        }
      } else if (regex_match(line, match, setting_line)) {
        if (match[1].str() == "hash_seed") {
          const auto bin_str = cereal::base64::decode(match[2]);
          assert(bin_str.length() == 8);
          seed = *reinterpret_cast<const uint64_t*>(bin_str.data());
        }
        txn.set_setting(match[1].str(), cereal::base64::decode(match[2]));
      } else if (regex_match(line, match, vote_line)) {
        txn.set_vote(stoull(match[1]), stoull(match[2]), static_cast<Vote>(std::stoi(match[3])));
      } else if (regex_match(line, match, subscription_line)) {
        txn.set_subscription(stoull(match[1]), stoull(match[2]), true);
      } else {
        throw runtime_error("Invalid line in database dump: " + line);
      }
    }
    txn.commit();
    on_error.canceled = true;
  }

  DB::~DB() {
    mdb_env_close(env);
  }

  static auto int_key(MDB_val& k, MDB_val&) -> uint64_t {
    return val_as<uint64_t>(k);
  };
  static auto second_key(MDB_val& k, MDB_val&) -> uint64_t {
    return Cursor(k).int_field_1();
  };
  static auto third_key(MDB_val& k, MDB_val&) -> uint64_t {
    return Cursor(k).int_field_2();
  };

  static inline auto count(MDB_dbi dbi, MDB_txn* txn, optional<Cursor> from = {}, optional<Cursor> to = {}) -> uint64_t {
    DBIter<void> iter(dbi, txn, from, to, [](MDB_val&, MDB_val&){});
    uint64_t n = 0;
    while (!iter.is_done()) {
      ++n;
      ++iter;
    }
    return n;
  }

  auto ReadTxnBase::get_setting_str(string_view key) -> string_view {
    MDB_val v;
    if (db_get(txn, db.dbis[Settings], key, v)) return {};
    return string_view(static_cast<const char*>(v.mv_data), v.mv_size);
  }
  auto ReadTxnBase::get_setting_int(string_view key) -> uint64_t {
    MDB_val v;
    if (db_get(txn, db.dbis[Settings], key, v)) return {};
    return val_as<uint64_t>(v);
  }

  auto ReadTxnBase::get_session(uint64_t session_id) -> OptRef<Session> {
    MDB_val v;
    if (db_get(txn, db.dbis[Session_Session], session_id, v)) {
      spdlog::debug("Session {:x} does not exist", session_id);
      return {};
    }
    const auto& session = *GetRoot<Session>(v.mv_data);
    if (session.expires_at() > now_s()) return { session };
    spdlog::debug("Session {:x} is expired", session_id);
    return {};
  }

  auto ReadTxnBase::get_user_id_by_name(string_view name) -> optional<uint64_t> {
    const auto name_lc = to_ascii_lowercase(name);
    return db_set_disambiguate_hash(txn, db.dbis[User_Name], Cursor(name_lc, db.seed), [&](auto id) {
      const auto user = get_user(id);
      return user && user->get().name()->string_view() == name_lc;
    });
  }
  auto ReadTxnBase::get_user_id_by_email(string_view email) -> optional<uint64_t> {
    const auto email_lc = to_ascii_lowercase(email);
    return db_set_disambiguate_hash(txn, db.dbis[User_Email], Cursor(email_lc, db.seed), [&](auto id) {
      const auto user = get_local_user(id);
      return user && user->get().email() && user->get().email()->string_view() == email_lc;
    });
  }
  auto ReadTxnBase::get_user(uint64_t id) -> OptRef<User> {
    MDB_val v;
    if (db_get(txn, db.dbis[User_User], id, v)) return {};
    return { *GetRoot<User>(v.mv_data) };
  }
  auto ReadTxnBase::get_user_stats(uint64_t id) -> OptRef<UserStats> {
    MDB_val v;
    if (db_get(txn, db.dbis[UserStats_User], id, v)) return {};
    return { *GetRoot<UserStats>(v.mv_data) };
  }
  auto ReadTxnBase::get_local_user(uint64_t id) -> OptRef<LocalUser> {
    MDB_val v;
    if (db_get(txn, db.dbis[LocalUser_User], id, v)) return {};
    return { *GetRoot<LocalUser>(v.mv_data) };
  }
  auto ReadTxnBase::count_local_users() -> uint64_t {
    return count(db.dbis[LocalUser_User], txn);
  }
  auto ReadTxnBase::list_users(OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[User_User], txn, cursor, {}, int_key);
  }
  auto ReadTxnBase::list_local_users(OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[LocalUser_User], txn, cursor, {}, int_key);
  }
  auto ReadTxnBase::list_subscribers(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Subscription_BoardUser],
      txn,
      cursor ? cursor : optional(Cursor(board_id, 0)),
      { Cursor(board_id, ID_MAX) },
      second_key
    );
  }
  auto ReadTxnBase::is_user_subscribed_to_board(uint64_t user_id, uint64_t board_id) -> bool {
    MDB_val v;
    return !db_get(txn, db.dbis[Subscription_UserBoard], Cursor(user_id, board_id), v);
  }

  auto ReadTxnBase::get_board_id_by_name(string_view name) -> optional<uint64_t> {
    const auto name_lc = to_ascii_lowercase(name);
    return db_set_disambiguate_hash(txn, db.dbis[Board_Name], Cursor(name_lc, db.seed), [&](auto id) {
      const auto board = get_board(id);
      return board && board->get().name()->string_view() == name_lc;
    });
  }
  auto ReadTxnBase::get_board(uint64_t id) -> OptRef<Board> {
    MDB_val v;
    if (db_get(txn, db.dbis[Board_Board], id, v)) return {};
    return { *GetRoot<Board>(v.mv_data) };
  }
  auto ReadTxnBase::get_board_stats(uint64_t id) -> OptRef<BoardStats> {
    MDB_val v;
    if (db_get(txn, db.dbis[BoardStats_Board], id, v)) return {};
    return { *GetRoot<BoardStats>(v.mv_data) };
  }
  auto ReadTxnBase::get_local_board(uint64_t id) -> OptRef<LocalBoard> {
    MDB_val v;
    if (db_get(txn, db.dbis[LocalBoard_Board], id, v)) return {};
    return { *GetRoot<LocalBoard>(v.mv_data) };
  }
  auto ReadTxnBase::count_local_boards() -> uint64_t {
    return count(db.dbis[LocalBoard_Board], txn);
  }
  auto ReadTxnBase::list_boards(OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[Board_Board], txn, cursor, {}, int_key);
  }
  auto ReadTxnBase::list_local_boards(OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[LocalBoard_Board], txn, cursor, {}, int_key);
  }
  auto ReadTxnBase::list_subscribed_boards(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Subscription_UserBoard],
      txn,
      cursor ? cursor : optional(Cursor(user_id, 0)),
      { Cursor(user_id, ID_MAX) },
      second_key
    );
  }
  auto ReadTxnBase::list_created_boards(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Owner_UserBoard],
      txn,
      cursor ? cursor : optional(Cursor(user_id, 0)),
      { Cursor(user_id, ID_MAX) },
      second_key
    );
  }

  auto ReadTxnBase::get_post_stats(uint64_t id) -> OptRef<PostStats> {
    MDB_val v;
    if (db_get(txn, db.dbis[PostStats_Post], id, v)) return {};
    return { *GetRoot<PostStats>(v.mv_data) };
  }
  auto ReadTxnBase::get_thread(uint64_t id) -> OptRef<Thread> {
    MDB_val v;
    if (db_get(txn, db.dbis[Thread_Thread], id, v)) return {};
    return { *GetRoot<Thread>(v.mv_data) };
  }
  auto ReadTxnBase::list_threads_of_board_new(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ThreadsNew_BoardTimeThread],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_threads_of_board_top(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ThreadsTop_BoardKarmaThread],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_threads_of_user_new(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[Owner_UserThread],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX)),
      { Cursor(user_id, 0) },
      second_key
    );
  }
  auto ReadTxnBase::list_threads_of_user_top(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ThreadsTop_UserKarmaThread],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX, ID_MAX)),
      { Cursor(user_id, 0, 0) }
    );
  }

  auto ReadTxnBase::get_comment(uint64_t id) -> OptRef<Comment> {
    MDB_val v;
    if (db_get(txn, db.dbis[Comment_Comment], id, v)) return {};
    return { *GetRoot<Comment>(v.mv_data) };
  }
  auto ReadTxnBase::list_comments_of_post_new(uint64_t post_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ChildrenNew_PostTimeComment],
      txn,
      cursor ? cursor : optional(Cursor(post_id, ID_MAX, ID_MAX)),
      { Cursor(post_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_comments_of_post_top(uint64_t post_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ChildrenTop_PostKarmaComment],
      txn,
      cursor ? cursor : optional(Cursor(post_id, ID_MAX, ID_MAX)),
      { Cursor(post_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_comments_of_board_new(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[CommentsNew_BoardTimeComment],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_comments_of_board_top(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[CommentsTop_BoardKarmaComment],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_comments_of_user_new(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[Owner_UserComment],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX)),
      { Cursor(user_id, 0) },
      second_key
    );
  }
  auto ReadTxnBase::list_comments_of_user_top(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[CommentsTop_UserKarmaComment],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX, ID_MAX)),
      { Cursor(user_id, 0, 0) }
    );
  }

  auto ReadTxnBase::get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote {
    MDB_val v;
    if (db_get(txn, db.dbis[Vote_UserPost], Cursor(user_id, post_id), v)) return NoVote;
    return (Vote)val_as<int8_t>(v);
  }

  auto ReadTxnBase::has_user_saved_post(uint64_t user_id, uint64_t post_id) -> bool {
    MDB_val v;
    return !db_get(txn, db.dbis[Save_UserPost], Cursor(user_id, post_id), v);
  }
  auto ReadTxnBase::has_user_hidden_post(uint64_t user_id, uint64_t post_id) -> bool {
    MDB_val v;
    return !db_get(txn, db.dbis[Hide_UserPost], Cursor(user_id, post_id), v);
  }
  auto ReadTxnBase::has_user_hidden_user(uint64_t user_id, uint64_t hidden_user_id) -> bool {
    MDB_val v;
    return !db_get(txn, db.dbis[Hide_UserUser], Cursor(user_id, hidden_user_id), v);
  }
  auto ReadTxnBase::has_user_hidden_board(uint64_t user_id, uint64_t board_id) -> bool {
    MDB_val v;
    return !db_get(txn, db.dbis[Hide_UserBoard], Cursor(user_id, board_id), v);
  }

  auto ReadTxnBase::get_application(uint64_t user_id) -> OptRef<Application> {
    MDB_val v;
    if (db_get(txn, db.dbis[Application_User], user_id, v)) return {};
    return { *GetRoot<Application>(v.mv_data) };
  }
  auto ReadTxnBase::list_applications(OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Application_User],
      txn,
      cursor ? cursor : optional(Cursor(0)), Cursor(ID_MAX)
    );
  }

  auto ReadTxnBase::get_invite(uint64_t invite_id) -> OptRef<Invite> {
    MDB_val v;
    if (db_get(txn, db.dbis[Invite_Invite], invite_id, v)) return {};
    return { *GetRoot<Invite>(v.mv_data) };
  }
  auto ReadTxnBase::list_invites_from_user(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[Application_User],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX)),
      Cursor(user_id, 0)
    );
  }

  static inline auto delete_range(
    MDB_txn* txn,
    MDB_dbi dbi,
    Cursor from,
    Cursor to,
    const std::function<void(MDB_val& k, MDB_val& v)>& fn = [](MDB_val&, MDB_val&){}
  ) -> void {
    MDBCursor cur(txn, dbi);
    MDB_val k = from.val(), v;
    auto err = mdb_cursor_get(cur, &k, &v, MDB_SET_RANGE);
    auto end = to.val();
    while (!err && mdb_cmp(txn, dbi, &k, &end) < 0) {
      fn(k, v);
      err = mdb_cursor_del(cur, 0) || mdb_cursor_get(cur, &k, &v, MDB_NEXT);
    }
  }

  auto WriteTxn::next_id() -> uint64_t {
    MDB_val v;
    db_get(txn, db.dbis[Settings], SettingsKey::next_id, v);
    const auto id = *static_cast<uint64_t*>(v.mv_data);
    db_put(txn, db.dbis[Settings], SettingsKey::next_id, id + 1);
    return id;
  }
  auto WriteTxn::set_setting(string_view key, string_view value) -> void {
    db_put(txn, db.dbis[Settings], key, value);
  }
  auto WriteTxn::set_setting(string_view key, uint64_t value) -> void {
    db_put(txn, db.dbis[Settings], key, value);
  }
  auto WriteTxn::create_session(
    uint64_t user,
    string_view ip,
    string_view user_agent,
    bool remember,
    uint64_t lifetime_seconds
  ) -> std::pair<uint64_t, uint64_t> {
    uint64_t id, now = now_s();
    if (!(++db.session_counter % 4)) {
      // Every 4 sessions, clean up old sessions.
      // TODO: Change this to 256; the low number is for testing.
      MDBCursor cur(txn, db.dbis[Session_Session]);
      MDB_val k, v;
      int err;
      for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = err || mdb_cursor_get(cur, &k, &v, MDB_NEXT)) {
        auto session = GetRoot<Session>(v.mv_data);
        if (session->expires_at() <= now) {
          spdlog::debug("Deleting expired session {:x} for user {:x}", val_as<uint64_t>(k), session->user());
          err = mdb_cursor_del(cur, 0);
        }
      }
      if (err && err != MDB_NOTFOUND) {
        spdlog::warn("Database error when deleting expired sessions: {}", mdb_strerror(err));
      }
    }
    duthomhas::csprng prng;
    prng(id);
    FlatBufferBuilder fbb;
    fbb.Finish(CreateSession(fbb,
      user,
      fbb.CreateString(ip),
      fbb.CreateString(user_agent),
      now,
      now + lifetime_seconds,
      remember
    ));
    db_put(txn, db.dbis[Session_Session], id, fbb);
    spdlog::debug("Created session {:x} for user {:x} (IP {}, user agent {})", id, user, ip, user_agent);
    return { id, now + lifetime_seconds };
  }
  auto WriteTxn::delete_session(uint64_t session_id) -> void {
    db_del(txn, db.dbis[Session_Session], session_id);
  }
  auto WriteTxn::create_user(FlatBufferBuilder& builder) -> uint64_t {
    const uint64_t id = next_id();
    set_user(id, builder);
    return id;
  }
  auto WriteTxn::set_user(uint64_t id, FlatBufferBuilder& builder) -> void {
    const auto& user = *GetRoot<User>(builder.GetBufferPointer());
    if (const auto old_user_opt = get_user(id)) {
      spdlog::debug("Updating user {:x} (name {})", id, user.name()->string_view());
      const auto& old_user = old_user_opt->get();
      if (user.name()->string_view() != old_user.name()->string_view()) {
        db_set_del(txn, db.dbis[User_Name], Cursor(old_user.name()->string_view(), db.seed), id);
      }
    } else {
      spdlog::debug("Creating user {:x} (name {})", id, user.name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateUserStats(fbb));
      db_put(txn, db.dbis[UserStats_User], id, fbb);
    }
    db_set_put(txn, db.dbis[User_Name], Cursor(user.name()->string_view(), db.seed), id);
    db_put(txn, db.dbis[User_User], id, builder);
  }
  auto WriteTxn::set_local_user(uint64_t id, FlatBufferBuilder& builder) -> void {
    const auto& user = *GetRoot<LocalUser>(builder.GetBufferPointer());
    if (const auto old_user_opt = get_local_user(id)) {
      const auto& old_user = old_user_opt->get();
      if (old_user.email() &&
        (!user.email() || user.email()->string_view() != old_user.email()->string_view())
      ) {
        db_set_del(txn, db.dbis[User_Email], Cursor(old_user.email()->string_view(), db.seed), id);
      }
    }
    db_set_put(txn, db.dbis[User_Email], Cursor(user.email()->string_view(), db.seed), id);
    db_put(txn, db.dbis[LocalUser_User], id, builder);
  }
  auto WriteTxn::delete_user(uint64_t id) -> bool {
    const auto user_opt = get_user(id);
    if (!user_opt) {
      spdlog::warn("Tried to delete nonexistent user {:x}", id);
      return false;
    }

    spdlog::debug("Deleting user {:x}", id);
    db_set_del(txn, db.dbis[User_Name], Cursor(user_opt->get().name()->string_view(), db.seed), id);
    db_del(txn, db.dbis[User_User], id);
    db_del(txn, db.dbis[UserStats_User], id);
    db_del(txn, db.dbis[Application_User], id);

    if (const auto local_user_opt = get_local_user(id)) {
      db_set_del(txn, db.dbis[User_Email], Cursor(local_user_opt->get().email()->string_view(), db.seed), id);
      db_del(txn, db.dbis[LocalUser_User], id);
    }

    delete_range(txn, db.dbis[Subscription_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX),
      [&](MDB_val& k, MDB_val&) {
        Cursor c(k);
        const auto board = c.int_field_1();
        db_del(txn, db.dbis[Subscription_BoardUser], Cursor(board, c.int_field_0()));
        if (const auto board_stats = get_board_stats(board)) {
          const auto& s = board_stats->get();
          FlatBufferBuilder fbb;
          fbb.Finish(CreateBoardStats(fbb,
            s.thread_count(),
            s.comment_count(),
            min(s.subscriber_count(), s.subscriber_count() - 1),
            s.users_active_half_year(),
            s.users_active_month(),
            s.users_active_week(),
            s.users_active_day()
          ));
          db_put(txn, db.dbis[BoardStats_Board], id, fbb);
        }
      }
    );
    delete_range(txn, db.dbis[Owner_UserInvite], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Owner_UserThread], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Owner_UserComment], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Owner_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Owner_UserMedia], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Hide_UserPost], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Hide_UserUser], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Hide_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[ThreadsTop_UserKarmaThread], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[CommentsTop_UserKarmaComment], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[Vote_UserPost], Cursor(id, 0), Cursor(id, ID_MAX));

    // TODO: Delete everything connected to the User
    // TODO: Does this delete owned posts and boards?
    return true;
  }

  auto WriteTxn::create_board(FlatBufferBuilder& builder) -> uint64_t {
    const uint64_t id = next_id();
    set_board(id, builder);
    return id;
  }
  auto WriteTxn::set_board(uint64_t id, FlatBufferBuilder& builder) -> void {
    const auto& board = *GetRoot<Board>(builder.GetBufferPointer());
    if (const auto old_board_opt = get_board(id)) {
      spdlog::debug("Updating board {:x} (name {})", id, board.name()->string_view());
      const auto& old_board = old_board_opt->get();
      if (board.name() != old_board.name()) {
        db_del(txn, db.dbis[Board_Name], Cursor(old_board.name()->string_view(), db.seed));
      }
    } else {
      spdlog::debug("Creating board {:x} (name {})", id, board.name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateBoardStats(fbb));
      db_put(txn, db.dbis[BoardStats_Board], id, fbb);
    }
    db_put(txn, db.dbis[Board_Board], id, builder);
    db_put(txn, db.dbis[Board_Name], Cursor(board.name()->string_view(), db.seed), id);
  }
  auto WriteTxn::set_local_board(uint64_t id, FlatBufferBuilder& builder) -> void {
    const auto& board = *GetRoot<LocalBoard>(builder.GetBufferPointer());
    assert_fmt(!!get_user(board.owner()), "set_local_board: board {:x} owner user {:x} does not exist", id, board.owner());
    if (const auto old_board_opt = get_local_board(id)) {
      spdlog::debug("Updating local board {:x}", id);
      const auto& old_board = old_board_opt->get();
      if (board.owner() != old_board.owner()) {
        spdlog::info("Changing owner of local board {:x}: {:x} -> {:x}", id, old_board.owner(), board.owner());
        db_del(txn, db.dbis[Owner_UserBoard], Cursor(old_board.owner(), id));
      }
    } else {
      spdlog::debug("Updating local board {:x}", id);
    }
    db_put(txn, db.dbis[Owner_UserBoard], Cursor(board.owner(), id), board.owner());
    db_put(txn, db.dbis[LocalBoard_Board], id, builder);
  }
  auto WriteTxn::delete_board(uint64_t id) -> bool {
    const auto board_opt = get_board(id);
    if (!board_opt) {
      spdlog::warn("Tried to delete nonexistent board {:x}", id);
      return false;
    }

    spdlog::debug("Deleting board {:x}", id);
    MDB_val id_val{ sizeof(uint64_t), &id };
    db_del(txn, db.dbis[Board_Board], id_val);
    db_del(txn, db.dbis[Board_Name], Cursor(board_opt->get().name()->string_view(), db.seed));
    db_del(txn, db.dbis[BoardStats_Board], id_val);
    db_del(txn, db.dbis[LocalBoard_Board], id_val);
    // TODO: Owner_UserBoard

    delete_range(txn, db.dbis[Subscription_BoardUser], Cursor(id, 0), Cursor(id, ID_MAX),
      [&](MDB_val& k, MDB_val&) {
        Cursor c(k);
        db_del(txn, db.dbis[Subscription_UserBoard], Cursor(c.int_field_1(), c.int_field_0()));
      }
    );
    delete_range(txn, db.dbis[ThreadsNew_BoardTimeThread], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[ThreadsTop_BoardKarmaThread], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[CommentsNew_BoardTimeComment], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[CommentsTop_BoardKarmaComment], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    return true;
  }
  auto WriteTxn::set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
    MDB_val v;
    const bool existing = !db_get(txn, db.dbis[Subscription_BoardUser], Cursor(board_id, user_id), v);
    const auto board_stats = get_board_stats(board_id);
    auto subscriber_count = board_stats ? board_stats->get().subscriber_count() : 0;
    if (subscribed) {
      assert_fmt(!!get_user(user_id), "set_subscription: user {:x} does not exist", user_id);
      assert_fmt(!!board_stats, "set_subscription: board {:x} does not exist", board_id);
      if (!existing) {
        spdlog::debug("Subscribing user {:x} to board {:x}", user_id, board_id);
        const auto now = now_s();
        db_put(txn, db.dbis[Subscription_BoardUser], Cursor(board_id, user_id), now);
        db_put(txn, db.dbis[Subscription_UserBoard], Cursor(user_id, board_id), now);
        subscriber_count++;
      }
    } else if (existing) {
      spdlog::debug("Unsubscribing user {:x} from board {:x}", user_id, board_id);
      db_del(txn, db.dbis[Subscription_BoardUser], Cursor(board_id, user_id));
      db_del(txn, db.dbis[Subscription_UserBoard], Cursor(user_id, board_id));
      subscriber_count = min(subscriber_count, subscriber_count - 1);
    }
    if (board_stats) {
      const auto& s = board_stats->get();
      FlatBufferBuilder fbb;
      fbb.Finish(CreateBoardStats(fbb,
        s.thread_count(),
        s.comment_count(),
        subscriber_count,
        s.users_active_half_year(),
        s.users_active_month(),
        s.users_active_week(),
        s.users_active_day()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board_id, fbb);
    }
  }
  auto WriteTxn::set_save(uint64_t user_id, uint64_t post_id, bool saved) -> void {
    assert_fmt(!!get_local_user(user_id), "set_save: local user {:x} does not exist", user_id);
    assert_fmt(!!get_post_stats(post_id), "set_save: post {:x} does not exist", post_id);
    if (saved) db_put(txn, db.dbis[Save_UserPost], Cursor(user_id, post_id), now_s());
    else db_del(txn, db.dbis[Save_UserPost], Cursor(user_id, post_id));
  }
  auto WriteTxn::set_hide_post(uint64_t user_id, uint64_t post_id, bool hidden) -> void {
    assert_fmt(!!get_local_user(user_id), "set_hide_post: local user {:x} does not exist", user_id);
    assert_fmt(!!get_post_stats(post_id), "set_hide_post: post {:x} does not exist", post_id);
    if (hidden) db_put(txn, db.dbis[Hide_UserPost], Cursor(user_id, post_id), now_s());
    else db_del(txn, db.dbis[Hide_UserPost], Cursor(user_id, post_id));
  }
  auto WriteTxn::set_hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden) -> void {
    assert_fmt(!!get_local_user(user_id), "set_hide_user: local user {:x} does not exist", user_id);
    assert_fmt(!!get_user(hidden_user_id), "set_hide_user: user {:x} does not exist", hidden_user_id);
    if (hidden) db_put(txn, db.dbis[Hide_UserUser], Cursor(user_id, hidden_user_id), now_s());
    else db_del(txn, db.dbis[Hide_UserUser], Cursor(user_id, hidden_user_id));
  }
  auto WriteTxn::set_hide_board(uint64_t user_id, uint64_t board_id, bool hidden) -> void {
    assert_fmt(!!get_local_user(user_id), "set_hide_board: local user {:x} does not exist", user_id);
    assert_fmt(!!get_board_stats(board_id), "set_hide_board: board {:x} does not exist", board_id);
    if (hidden) db_put(txn, db.dbis[Hide_UserBoard], Cursor(user_id, board_id), now_s());
    else db_del(txn, db.dbis[Hide_UserBoard], Cursor(user_id, board_id));
  }

  auto WriteTxn::create_thread(FlatBufferBuilder& builder) -> uint64_t {
    uint64_t id = next_id();
    set_thread(id, builder);
    return id;
  }
  auto WriteTxn::set_thread(uint64_t id, FlatBufferBuilder& builder) -> void {
    const auto& thread = *GetRoot<Thread>(builder.GetBufferPointer());
    FlatBufferBuilder fbb;
    int64_t karma = 0;
    if (const auto old_thread_opt = get_thread(id)) {
      spdlog::debug("Updating top-level post {:x} (board {:x}, author {:x})", id, thread.board(), thread.author());
      const auto stats_opt = get_post_stats(id);
      assert_fmt(!!stats_opt, "set_thread: post_stats not in database for existing thread {:x}", id);
      karma = stats_opt->get().karma();
      const auto& old_thread = old_thread_opt->get();
      assert_fmt(thread.author() == old_thread.author(), "set_thread: cannot change author of thread {:x}", id);
      assert_fmt(thread.created_at() == old_thread.created_at(), "set_thread: cannot change created_at of thread {:x}", id);
      if (thread.board() != old_thread.board()) {
        db_del(txn, db.dbis[ThreadsNew_BoardTimeThread], Cursor(old_thread.board(), thread.created_at(), id));
        db_del(txn, db.dbis[ThreadsTop_BoardKarmaThread], Cursor(old_thread.board(), karma_uint(karma), id));
        if (const auto board_stats = get_board_stats(old_thread.board())) {
          const auto& s = board_stats->get();
          fbb.Finish(CreateBoardStats(fbb,
            min(s.thread_count(), s.thread_count() - 1),
            s.comment_count(),
            s.subscriber_count(),
            s.users_active_half_year(),
            s.users_active_month(),
            s.users_active_week(),
            s.users_active_day()
          ));
          db_put(txn, db.dbis[BoardStats_Board], old_thread.board(), fbb);
        }
      }
    } else {
      spdlog::debug("Creating top-level post {:x} (board {:x}, author {:x})", id, thread.board(), thread.author());
      db_put(txn, db.dbis[Owner_UserThread], Cursor(thread.author(), id), thread.author());
      db_put(txn, db.dbis[ThreadsTop_UserKarmaThread], Cursor(thread.author(), 1, id), id);
      db_put(txn, db.dbis[ThreadsNew_BoardTimeThread], Cursor(thread.board(), thread.created_at(), id), id);
      db_put(txn, db.dbis[ThreadsTop_BoardKarmaThread], Cursor(thread.board(), karma_uint(karma), id), id);
      db_put(txn, db.dbis[ThreadsMostComments_BoardCommentsThread], Cursor(thread.board(), 0, id), id);
      fbb.ForceDefaults(true);
      fbb.Finish(CreatePostStats(fbb, thread.created_at()));
      db_put(txn, db.dbis[PostStats_Post], id, fbb);
      if (const auto user_stats = get_user_stats(thread.author())) {
        const auto& s = user_stats->get();
        fbb.Clear();
        fbb.Finish(CreateUserStats(fbb,
          s.comment_count(),
          s.comment_karma(),
          s.thread_count() + 1,
          s.thread_karma()
        ));
        db_put(txn, db.dbis[UserStats_User], thread.author(), fbb);
      }
      if (const auto board_stats = get_board_stats(thread.board())) {
        const auto& s = board_stats->get();
        fbb.Clear();
        fbb.Finish(CreateBoardStats(fbb,
          s.thread_count() + 1,
          s.comment_count(),
          s.subscriber_count(),
          s.users_active_half_year(),
          s.users_active_month(),
          s.users_active_week(),
          s.users_active_day()
        ));
        db_put(txn, db.dbis[BoardStats_Board], thread.board(), fbb);
      }
    }
    db_put(txn, db.dbis[Thread_Thread], id, builder);
  }
  auto WriteTxn::delete_child_comment(uint64_t id, uint64_t board_id) -> uint64_t {
    const auto comment_opt = get_comment(id);
    const auto stats_opt = get_post_stats(id);
    if (!comment_opt || !stats_opt) {
      spdlog::warn("Tried to delete nonexistent comment {:x}", id);
      return 0;
    }
    const auto& comment = comment_opt->get();
    const auto& stats = stats_opt->get();
    const auto karma = stats.karma();
    const auto descendant_count = stats.descendant_count(),
      author = comment.author(),
      created_at = comment.created_at(),
      parent = comment.parent();

    spdlog::debug("Deleting comment {:x} (parent {:x}, author {:x}, board {:x})", id, parent, author, board_id);
    if (const auto user_stats = get_user_stats(author)) {
      const auto& s = user_stats->get();
      FlatBufferBuilder fbb;
      fbb.Finish(CreateUserStats(fbb,
        min(s.comment_count(), s.comment_count() - 1),
        karma > 0 ? min(s.comment_karma(), s.comment_karma() - karma) : s.comment_karma() - karma,
        s.thread_count(),
        s.thread_karma()
      ));
      db_put(txn, db.dbis[UserStats_User], comment.author(), fbb);
    }
    db_del(txn, db.dbis[CommentsNew_BoardTimeComment], Cursor(board_id, created_at, id));
    db_del(txn, db.dbis[CommentsTop_BoardKarmaComment], Cursor(board_id, karma_uint(karma), id));
    db_del(txn, db.dbis[CommentsMostComments_BoardCommentsComment], Cursor(board_id, descendant_count, id));

    std::set<uint64_t> children;
    delete_range(txn, db.dbis[ChildrenNew_PostTimeComment], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX),
      [&children](MDB_val&, MDB_val& v) {
        children.insert(val_as<uint64_t>(v));
      }
    );
    delete_range(txn, db.dbis[ChildrenTop_PostKarmaComment], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    db_del(txn, db.dbis[Comment_Comment], id);
    db_del(txn, db.dbis[PostStats_Post], id);
    db_del(txn, db.dbis[Owner_UserComment], Cursor(author, id));
    db_del(txn, db.dbis[ChildrenNew_PostTimeComment], Cursor(parent, created_at, id));
    db_del(txn, db.dbis[ChildrenTop_PostKarmaComment], Cursor(parent, karma_uint(karma), id));

    uint64_t n = 0;
    for (uint64_t child : children) {
      assert(child != id);
      n += delete_child_comment(child, board_id);
    }
    return n;
  }
  auto WriteTxn::delete_thread(uint64_t id) -> bool {
    const auto thread_opt = get_thread(id);
    const auto stats_opt = get_post_stats(id);
    if (!thread_opt || !stats_opt) {
      spdlog::warn("Tried to delete nonexistent top-level post {:x}", id);
      return false;
    }
    const auto& thread = thread_opt->get();
    const auto& stats = stats_opt->get();
    const auto karma = stats.karma();
    const auto author = thread.author(),
      board = thread.board(),
      created_at = thread.created_at(),
      descendant_count = stats.descendant_count();

    spdlog::debug("Deleting top-level post {:x} (board {:x}, author {:x})", id, board, author);
    FlatBufferBuilder fbb;
    if (const auto user_stats = get_user_stats(author)) {
      const auto& s = user_stats->get();
      fbb.Finish(CreateUserStats(fbb,
        s.comment_count(),
        s.comment_karma(),
        min(s.thread_count(), s.thread_count() - 1),
        karma > 0 ? min(s.thread_karma(), s.thread_karma() - karma) : s.thread_karma() - karma
      ));
      db_put(txn, db.dbis[UserStats_User], author, fbb);
      fbb.Clear();
    }
    if (const auto board_stats = get_board_stats(board)) {
      const auto& s = board_stats->get();
      fbb.Finish(CreateBoardStats(fbb,
        min(s.thread_count(), s.thread_count() - 1),
        min(s.comment_count(), s.comment_count() - descendant_count),
        s.subscriber_count(),
        s.users_active_half_year(),
        s.users_active_month(),
        s.users_active_week(),
        s.users_active_day()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board, fbb);
      fbb.Clear();
    }

    // TODO: Delete dangling votes?
    // There used to be a bidirectional User<->Post index for votes,
    // but that almost doubled the size of the database.

    std::set<uint64_t> children;
    delete_range(txn, db.dbis[ChildrenNew_PostTimeComment], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX),
      [&children](MDB_val&, MDB_val& v) {
        children.insert(val_as<uint64_t>(v));
      }
    );
    delete_range(txn, db.dbis[ChildrenTop_PostKarmaComment], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    db_del(txn, db.dbis[Thread_Thread], id);
    db_del(txn, db.dbis[PostStats_Post], id);
    db_del(txn, db.dbis[Owner_UserThread], Cursor(author, id));
    db_del(txn, db.dbis[ThreadsTop_UserKarmaThread], Cursor(author, karma_uint(karma), id));
    db_del(txn, db.dbis[ThreadsNew_BoardTimeThread], Cursor(board, created_at, id));
    db_del(txn, db.dbis[ThreadsTop_BoardKarmaThread], Cursor(board, karma_uint(karma), id));
    db_del(txn, db.dbis[ThreadsMostComments_BoardCommentsThread], Cursor(board, descendant_count, id));

    for (uint64_t child : children) delete_child_comment(child, board);

    return true;
  }

  auto WriteTxn::create_comment(FlatBufferBuilder& builder) -> uint64_t {
    uint64_t id = next_id();
    set_comment(id, builder);
    return id;
  }
  auto WriteTxn::set_comment(uint64_t id, FlatBufferBuilder& builder) -> void {
    const auto& comment = *GetRoot<Comment>(builder.GetBufferPointer());
    const auto stats_opt = get_post_stats(id);
    const auto thread_opt = get_thread(comment.thread());
    int64_t karma = 0;
    assert_fmt(!!thread_opt, "set_comment: comment {:x} top-level ancestor thread {:x} does not exist", id, comment.thread());
    const auto& thread = thread_opt->get();
    const auto author = comment.author(), board = thread.board();
    if (const auto old_comment_opt = get_comment(id)) {
      spdlog::debug("Updating comment {:x} (parent {:x}, author {:x})", id, comment.parent(), comment.author());
      assert(!!stats_opt);
      const auto& old_comment = old_comment_opt->get();
      const auto& stats = stats_opt->get();
      karma = stats.karma();
      assert(comment.author() == old_comment.author());
      assert(comment.parent() == old_comment.parent());
      assert(comment.thread() == old_comment.thread());
      assert(comment.created_at() == old_comment.created_at());
    } else {
      spdlog::debug("Creating comment {:x} (parent {:x}, author {:x})", id, comment.parent(), comment.author());
      db_put(txn, db.dbis[Owner_UserComment], Cursor(author, id), author);
      db_put(txn, db.dbis[CommentsTop_UserKarmaComment], Cursor(author, karma_uint(karma), id), id);
      db_put(txn, db.dbis[CommentsNew_BoardTimeComment], Cursor(board, comment.created_at(), id), id);
      db_put(txn, db.dbis[CommentsTop_BoardKarmaComment], Cursor(board, karma_uint(karma), id), id);
      db_put(txn, db.dbis[ChildrenNew_PostTimeComment], Cursor(comment.parent(), comment.created_at(), id), id);
      db_put(txn, db.dbis[ChildrenTop_PostKarmaComment], Cursor(comment.parent(), karma_uint(karma), id), id);
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreatePostStats(fbb, comment.created_at()));
      db_put(txn, db.dbis[PostStats_Post], id, fbb);

      for (OptRef<Comment> comment_opt = {comment}; comment_opt; comment_opt = get_comment(comment_opt->get().parent())) {
        const auto parent = comment_opt->get().parent();
        if (const auto parent_stats_opt = get_post_stats(parent)) {
          uint64_t parent_created_at;
          if (const auto parent_opt = get_comment(parent)) parent_created_at = parent_opt->get().created_at();
          else if (parent == comment.thread()) parent_created_at = thread.created_at();
          else continue;
          const auto& s = parent_stats_opt->get();
          const bool is_active = comment.created_at() >= parent_created_at &&
              comment.created_at() - parent_created_at <= ACTIVE_COMMENT_MAX_AGE,
            is_newer = is_active && comment.created_at() > s.latest_comment();
          const auto last_descendant_count = s.descendant_count();
          fbb.Clear();
          fbb.Finish(CreatePostStats(fbb,
            is_newer ? comment.created_at() : s.latest_comment(),
            is_active ? s.latest_comment_necro() : std::max(s.latest_comment_necro(), comment.created_at()),
            s.descendant_count() + 1,
            s.child_count() + 1,
            s.upvotes(),
            s.downvotes(),
            s.karma()
          ));
          db_put(txn, db.dbis[PostStats_Post], parent, fbb);
          if (parent == comment.thread()) {
            db_del(txn, db.dbis[ThreadsMostComments_BoardCommentsThread], Cursor(board, last_descendant_count, parent));
            db_put(txn, db.dbis[ThreadsMostComments_BoardCommentsThread], Cursor(board, last_descendant_count + 1, parent), parent);
          } else {
            db_del(txn, db.dbis[CommentsMostComments_BoardCommentsComment], Cursor(board, last_descendant_count, parent));
            db_put(txn, db.dbis[CommentsMostComments_BoardCommentsComment], Cursor(board, last_descendant_count + 1, parent), parent);
          }
        }
      }
      if (const auto user_stats = get_user_stats(author)) {
        const auto& s = user_stats->get();
        fbb.Clear();
        fbb.Finish(CreateUserStats(fbb,
          s.comment_count() + 1,
          s.comment_karma(),
          s.thread_count(),
          s.thread_karma()
        ));
        db_put(txn, db.dbis[UserStats_User], author, fbb);
      }
      if (const auto board_stats = get_board_stats(board)) {
        const auto& s = board_stats->get();
        fbb.Clear();
        fbb.Finish(CreateBoardStats(fbb,
          s.thread_count(),
          s.comment_count() + 1,
          s.subscriber_count(),
          s.users_active_half_year(),
          s.users_active_month(),
          s.users_active_week(),
          s.users_active_day()
        ));
        db_put(txn, db.dbis[BoardStats_Board], board, fbb);
      }
    }
    db_put(txn, db.dbis[Comment_Comment], id, builder);
  }
  auto WriteTxn::delete_comment(uint64_t id) -> uint64_t {
    const auto comment_opt = get_comment(id);
    const auto stats_opt = get_post_stats(id);
    if (!comment_opt || !stats_opt) {
      spdlog::warn("Tried to delete nonexistent comment {:x}", id);
      return false;
    }
    const auto& comment = comment_opt->get();
    const auto& stats = stats_opt->get();
    const auto thread_id = comment.thread();
    const auto thread_opt = get_thread(thread_id);
    assert_fmt(!!thread_opt, "delete_comment: comment {:x} top-level ancestor thread {:x} does not exist", id, thread_id);
    const auto board_id = thread_opt->get().board(),
      descendant_count = stats.descendant_count();

    FlatBufferBuilder fbb;
    for (OptRef<Comment> comment_opt = {comment}; comment_opt; comment_opt = get_comment(comment_opt->get().parent())) {
      const auto parent = comment_opt->get().parent();
      if (const auto parent_stats_opt = get_post_stats(parent)) {
        const auto& s = parent_stats_opt->get();
        const auto last_descendant_count = s.descendant_count(),
          next_descendant_count =
            (descendant_count + 1) > s.descendant_count() ? 0 : s.descendant_count() - (descendant_count + 1);
        fbb.Clear();
        fbb.Finish(CreatePostStats(fbb,
          s.latest_comment(),
          s.latest_comment_necro(),
          next_descendant_count,
          parent == comment.parent()
            ? min(s.child_count(), s.child_count() - 1)
            : s.child_count(),
          s.upvotes(),
          s.downvotes(),
          s.karma()
        ));
        db_put(txn, db.dbis[PostStats_Post], parent, fbb);
        if (parent == comment.thread()) {
          db_del(txn, db.dbis[ThreadsMostComments_BoardCommentsThread], Cursor(board_id, last_descendant_count, parent));
          db_put(txn, db.dbis[ThreadsMostComments_BoardCommentsThread], Cursor(board_id, next_descendant_count, parent), parent);
        } else {
          db_del(txn, db.dbis[CommentsMostComments_BoardCommentsComment], Cursor(board_id, last_descendant_count, parent));
          db_put(txn, db.dbis[CommentsMostComments_BoardCommentsComment], Cursor(board_id, next_descendant_count, parent), parent);
        }
      }
    }
    if (const auto board_stats_opt = get_board_stats(board_id)) {
      const auto& s = board_stats_opt->get();
      fbb.Clear();
      fbb.Finish(CreateBoardStats(fbb,
        s.thread_count(),
        (descendant_count + 1) > s.comment_count() ? 0 : s.comment_count() - (descendant_count + 1),
        s.subscriber_count(),
        s.users_active_half_year(),
        s.users_active_month(),
        s.users_active_week(),
        s.users_active_day()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board_id, fbb);
    }

    return delete_child_comment(id, board_id);
  }

  auto WriteTxn::set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    const auto existing = get_vote_of_user_for_post(user_id, post_id);
    const int64_t diff = vote - existing;
    if (!diff) return;
    const auto thread_opt = get_thread(post_id);
    const auto comment_opt = thread_opt ? nullopt : get_comment(post_id);
    assert(thread_opt || comment_opt);
    const auto op_id = thread_opt ? thread_opt->get().author() : comment_opt->get().author();
    spdlog::debug("Setting vote from user {:x} on post {:x} to {}", user_id, post_id, (int8_t)vote);
    if (vote) {
      int8_t vote_byte = (int8_t)vote;
      MDB_val v { sizeof(int8_t), &vote_byte };
      db_put(txn, db.dbis[Vote_UserPost], Cursor(user_id, post_id), v);
    } else {
      db_del(txn, db.dbis[Vote_UserPost], Cursor(user_id, post_id));
    }
    int64_t old_karma, new_karma;
    FlatBufferBuilder fbb;
    if (const auto stats_opt = get_post_stats(post_id)) {
      const auto& s = stats_opt->get();
      old_karma = s.karma(), new_karma = old_karma + diff;
      fbb.Finish(CreatePostStats(fbb,
        s.latest_comment(),
        s.latest_comment_necro(),
        s.descendant_count(),
        s.child_count(),
        vote > 0 ? s.upvotes() + 1 : (existing > 0 ? min(s.upvotes(), s.upvotes() - 1) : s.upvotes()),
        vote < 0 ? s.downvotes() + 1 : (existing < 0 ? min(s.downvotes(), s.downvotes() - 1) : s.downvotes()),
        new_karma
      ));
      db_put(txn, db.dbis[PostStats_Post], post_id, fbb);
    }
    if (const auto op_stats_opt = get_user_stats(op_id)) {
      const auto& s = op_stats_opt->get();
      fbb.Clear();
      fbb.Finish(CreateUserStats(fbb,
        s.comment_count(),
        s.comment_karma() + (thread_opt ? 0 : diff),
        s.thread_count(),
        s.thread_karma() + (thread_opt ? diff : 0)
      ));
      db_put(txn, db.dbis[UserStats_User], op_id, fbb);
    }
    if (thread_opt) {
      const auto& thread = thread_opt->get();
      db_del(txn, db.dbis[ThreadsTop_BoardKarmaThread], Cursor(thread.board(), karma_uint(old_karma), post_id));
      db_del(txn, db.dbis[ThreadsTop_UserKarmaThread], Cursor(thread.author(), karma_uint(old_karma), post_id));
      db_put(txn, db.dbis[ThreadsTop_BoardKarmaThread], Cursor(thread.board(), karma_uint(new_karma), post_id), post_id);
      db_put(txn, db.dbis[ThreadsTop_UserKarmaThread], Cursor(thread.author(), karma_uint(new_karma), post_id), post_id);
    } else {
      const auto& comment = comment_opt->get();
      db_del(txn, db.dbis[CommentsTop_UserKarmaComment], Cursor(comment.author(), karma_uint(old_karma), post_id));
      db_del(txn, db.dbis[ChildrenTop_PostKarmaComment], Cursor(comment.parent(), karma_uint(old_karma), post_id));
      db_put(txn, db.dbis[CommentsTop_UserKarmaComment], Cursor(comment.author(), karma_uint(new_karma), post_id), post_id);
      db_put(txn, db.dbis[ChildrenTop_PostKarmaComment], Cursor(comment.parent(), karma_uint(new_karma), post_id), post_id);
      if (const auto comment_thread_opt = get_thread(comment.thread())) {
        const auto& comment_thread = comment_thread_opt->get();
        db_put(txn, db.dbis[CommentsTop_BoardKarmaComment], Cursor(comment_thread.board(), karma_uint(new_karma), post_id), post_id);
        db_del(txn, db.dbis[CommentsTop_BoardKarmaComment], Cursor(comment_thread.board(), karma_uint(old_karma), post_id));
      }
    }
  }

  auto WriteTxn::create_application(uint64_t user_id, FlatBufferBuilder& builder) -> void {
    assert_fmt(!!get_local_user(user_id), "create_application: local user {:x} does not exist", user_id);
    db_put(txn, db.dbis[Application_User], user_id, builder);
  }
  auto WriteTxn::create_invite(uint64_t sender_user_id, uint64_t lifetime_seconds) -> uint64_t {
    const uint64_t id = next_id(), now = now_s();
    FlatBufferBuilder fbb;
    fbb.Finish(CreateInvite(fbb, now, now + lifetime_seconds, sender_user_id));
    set_invite(id, fbb);
    return id;
  }
  auto WriteTxn::set_invite(uint64_t invite_id, FlatBufferBuilder& builder) -> void {
    const auto invite = GetRoot<Invite>(builder.GetBufferPointer());
    if (const auto old_invite = get_invite(invite_id)) {
      assert_fmt(invite->created_at() == old_invite->get().created_at(), "set_invite: cannot change created_at field of invite");
      assert_fmt(invite->from() == old_invite->get().from(), "set_invite: cannot change from field of invite");
    } else {
      assert_fmt(!!get_local_user(invite->from()), "set_invite: local user {:x} does not exist", invite->from());
      db_put(txn, db.dbis[Owner_UserInvite], Cursor(invite->from(), invite_id), invite->from());
    }
    db_put(txn, db.dbis[Invite_Invite], invite_id, builder);
  }
  auto WriteTxn::delete_invite(uint64_t invite_id) -> void {
    if (auto invite = get_invite(invite_id)) {
      db_del(txn, db.dbis[Owner_UserInvite], Cursor(invite->get().from(), invite_id));
    }
    db_del(txn, db.dbis[Invite_Invite], invite_id);
  }
}
