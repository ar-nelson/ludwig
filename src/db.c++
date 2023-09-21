#include <random>
#include <regex>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <duthomhas/csprng.hpp>
#include <flatbuffers/idl.h>
#include "generated/datatypes.fbs.h"
#include "db.h++"

using std::optional, std::string_view, flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot;

#define assert_fmt(CONDITION, ...) if (!(CONDITION)) { spdlog::critical(__VA_ARGS__); throw std::runtime_error("Assertion failed: " #CONDITION); }

namespace Ludwig {
  static const std::regex
    json_line(R"(^([uUbBpndarm]) (\d+) (\{[^\n]+\})$)", std::regex::ECMAScript),
    setting_line(R"(^S (\w+) ([0-9a-zA-Z+/]+=*)$)", std::regex::ECMAScript),
    vote_line(R"(^v (\d+) (\d+) (1|-1)$)", std::regex::ECMAScript),
    subscription_line(R"(^s (\d+) (\d+) (\d+)$)", std::regex::ECMAScript);

  static constexpr uint64_t ACTIVE_COMMENT_MAX_AGE = 86400 * 2; // 2 days

  enum Dbi {
    Settings,
    Session_Session,

    User_User,
    User_Name,
    UserStats_User,
    LocalUser_User,
    Owner_UserBoard,
    Owner_UserPage,
    Owner_UserNote,
    PagesTop_UserKarmaPage,
    NotesTop_UserKarmaNote,
    Bookmark_UserPost,
    Hide_UserPost,
    Hide_UserUser,
    Hide_UserBoard,
    Subscription_UserBoard,
    Owner_UserMedia,

    Board_Board,
    Board_Name,
    BoardStats_Board,
    LocalBoard_Board,
    PagesTop_BoardKarmaPage,
    PagesNew_BoardTimePage,
    PagesNewComments_BoardTimePage,
    PagesMostComments_BoardCommentsPage,
    NotesTop_BoardKarmaNote,
    NotesNew_BoardTimeNote,
    NotesNewComments_BoardTimeNote,
    NotesMostComments_BoardCommentsNote,
    Subscription_BoardUser,

    Page_Page,
    Note_Note,
    PostStats_Post,
    ChildrenNew_PostTimeNote,
    ChildrenTop_PostKarmaNote,
    Contains_PostMedia,

    Vote_UserPost,
    Vote_PostUser,

    Media_Media,
    Contains_MediaPost,
  };

  inline auto db_get(MDB_txn* txn, MDB_dbi dbi, MDB_val& k, MDB_val& v) -> int {
    return mdb_get(txn, dbi, &k, &v);
  }

  inline auto db_get(MDB_txn* txn, MDB_dbi dbi, string_view k, MDB_val& v) -> int {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    return mdb_get(txn, dbi, &kval, &v);
  }

  inline auto db_get(MDB_txn* txn, MDB_dbi dbi, uint64_t k, MDB_val& v) -> int {
    MDB_val kval { sizeof(uint64_t), &k };
    return mdb_get(txn, dbi, &kval, &v);
  }

  inline auto db_get(MDB_txn* txn, MDB_dbi dbi, Cursor k, MDB_val& v) -> int {
    MDB_val kval = k.val();
    return mdb_get(txn, dbi, &kval, &v);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, MDB_val& k, MDB_val& v, unsigned flags = 0) -> void {
    if (auto err = mdb_put(txn, dbi, &k, &v, flags)) throw DBError("Write failed", err);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, MDB_val& v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    db_put(txn, dbi, kval, v, flags);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, string_view v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    MDB_val vval { v.length(), const_cast<char*>(v.data()) };
    db_put(txn, dbi, kval, vval, flags);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, uint64_t v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    MDB_val vval { sizeof(uint64_t), &v };
    db_put(txn, dbi, kval, vval, flags);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v, unsigned flags = 0) -> void {
    MDB_val kval = k.val();
    MDB_val vval { sizeof(uint64_t), &v };
    db_put(txn, dbi, kval, vval, flags);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, uint64_t k, FlatBufferBuilder& fbb, unsigned flags = 0) -> void {
    MDB_val kval { sizeof(uint64_t), &k };
    MDB_val vval { fbb.GetSize(), fbb.GetBufferPointer() };
    db_put(txn, dbi, kval, vval, flags);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, Cursor k, MDB_val& v, unsigned flags = 0) -> void {
    MDB_val kval = k.val();
    db_put(txn, dbi, kval, v, flags);
  }

  inline auto db_del(MDB_txn* txn, MDB_dbi dbi, Cursor k) -> void {
    MDB_val kval = k.val();
    if (auto err = mdb_del(txn, dbi, &kval, nullptr)) {
      if (err != MDB_NOTFOUND) throw DBError("Delete failed", err);
    }
  }

  inline auto db_del(MDB_txn* txn, MDB_dbi dbi, uint64_t k) -> void {
    MDB_val kval = { sizeof(uint64_t), &k };
    if (auto err = mdb_del(txn, dbi, &kval, nullptr)) {
      if (err != MDB_NOTFOUND) throw DBError("Delete failed", err);
    }
  }

  struct MDBCursor {
    MDB_cursor* cur;
    MDBCursor(MDB_txn* txn, MDB_dbi dbi) {
      auto err = mdb_cursor_open(txn, dbi, &cur);
      if (err) throw DBError("Failed to open database cursor", err);
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
    MK_DBI(UserStats_User, MDB_INTEGERKEY)
    MK_DBI(LocalUser_User, MDB_INTEGERKEY)
    MK_DBI(Owner_UserBoard, 0)
    MK_DBI(Owner_UserPage, 0)
    MK_DBI(Owner_UserNote, 0)
    MK_DBI(PagesTop_UserKarmaPage, 0)
    MK_DBI(NotesTop_UserKarmaNote, 0)
    MK_DBI(Bookmark_UserPost, 0)
    MK_DBI(Hide_UserPost, 0)
    MK_DBI(Hide_UserUser, 0)
    MK_DBI(Hide_UserBoard, 0)
    MK_DBI(Subscription_UserBoard, 0)
    MK_DBI(Owner_UserMedia, 0)

    MK_DBI(Board_Board, MDB_INTEGERKEY)
    MK_DBI(Board_Name, MDB_INTEGERKEY)
    MK_DBI(BoardStats_Board, MDB_INTEGERKEY)
    MK_DBI(LocalBoard_Board, MDB_INTEGERKEY)
    MK_DBI(PagesTop_BoardKarmaPage, 0)
    MK_DBI(PagesNew_BoardTimePage, 0)
    MK_DBI(PagesNewComments_BoardTimePage, 0)
    MK_DBI(PagesMostComments_BoardCommentsPage, 0)
    MK_DBI(NotesTop_BoardKarmaNote, 0)
    MK_DBI(NotesNew_BoardTimeNote, 0)
    MK_DBI(NotesNewComments_BoardTimeNote, 0)
    MK_DBI(NotesMostComments_BoardCommentsNote, 0)
    MK_DBI(Subscription_BoardUser, 0)

    MK_DBI(Page_Page, MDB_INTEGERKEY)
    MK_DBI(Note_Note, MDB_INTEGERKEY)
    MK_DBI(PostStats_Post, MDB_INTEGERKEY)
    MK_DBI(ChildrenNew_PostTimeNote, 0)
    MK_DBI(ChildrenTop_PostKarmaNote, 0)
    MK_DBI(Contains_PostMedia, 0)

    MK_DBI(Vote_UserPost, 0)
    MK_DBI(Vote_PostUser, 0)

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
        throw std::runtime_error("Cannot import database dump: database file " +
            std::string(filename) + " already exists and would be overwritten.");
      }
    }

    flatbuffers::Parser parser;
    parser.Parse(
      std::string(std::string_view(reinterpret_cast<char*>(datatypes_fbs), datatypes_fbs_len)).c_str(),
      nullptr,
      "datatypes.fbs"
    );

    {
      MDB_txn* txn = nullptr;
      int err = init_env(filename, &txn) || mdb_txn_commit(txn);
      if (err) {
        if (txn != nullptr) mdb_txn_abort(txn);
        mdb_env_close(env);
        remove(filename);
        throw DBError("Failed to open database", err);
      }
    }

    DeferDelete on_error{ env, filename };
    auto txn = open_write_txn();
    std::string line;
    seed = 0;
    while (std::getline(dump_stream, line)) {
      if (line.empty()) continue;
      std::smatch match;
      if (std::regex_match(line, match, json_line)) {
        switch (match[1].str()[0]) {
          case 'u':
            if (seed == 0) {
              spdlog::warn("hash_seed was not set before creating users, name lookup may be broken");
            }
            parser.SetRootType("User");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw std::runtime_error("Failed to parse User JSON: " + match[3].str());
            }
            txn.set_user(std::stoull(match[2]), parser.builder_);
            break;
          case 'U':
            parser.SetRootType("LocalUser");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw std::runtime_error("Failed to parse LocalUser JSON: " + match[3].str());
            }
            txn.set_local_user(std::stoull(match[2]), parser.builder_);
            break;
          case 'b':
            if (seed == 0) {
              spdlog::warn("hash_seed was not set before creating boards, name lookup may be broken");
            }
            parser.SetRootType("Board");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw std::runtime_error("Failed to parse Board JSON: " + match[3].str());
            }
            txn.set_board(std::stoull(match[2]), parser.builder_);
            break;
          case 'B':
            parser.SetRootType("LocalBoard");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw std::runtime_error("Failed to parse LocalBoard JSON: " + match[3].str());
            }
            txn.set_local_board(std::stoull(match[2]), parser.builder_);
            break;
          case 'p':
            parser.SetRootType("Page");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw std::runtime_error("Failed to parse Page JSON: " + match[3].str());
            }
            txn.set_page(std::stoull(match[2]), parser.builder_);
            break;
          case 'n':
            parser.SetRootType("Note");
            if (!parser.ParseJson(match[3].str().c_str())) {
              throw std::runtime_error("Failed to parse Note JSON: " + match[3].str());
            }
            txn.set_note(std::stoull(match[2]), parser.builder_);
            break;
        }
      } else if (std::regex_match(line, match, setting_line)) {
        if (match[1].str() == "hash_seed") {
          const auto bin_str = cereal::base64::decode(match[2]);
          assert(bin_str.length() == 8);
          seed = *reinterpret_cast<const uint64_t*>(bin_str.data());
        }
        txn.set_setting(match[1].str(), cereal::base64::decode(match[2]));
      } else if (std::regex_match(line, match, vote_line)) {
        txn.set_vote(std::stoull(match[1]), std::stoull(match[2]), static_cast<Vote>(std::stoi(match[3])));
      } else if (std::regex_match(line, match, subscription_line)) {
        txn.set_subscription(std::stoull(match[1]), std::stoull(match[2]), true);
      } else {
        throw std::runtime_error("Invalid line in database dump: " + line);
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

  auto ReadTxnBase::validate_session(uint64_t session_id) -> optional<uint64_t> {
    MDB_val v;
    if (db_get(txn, db.dbis[Session_Session], session_id, v)) {
      spdlog::debug("Session {:x} does not exist", session_id);
      return {};
    }
    const auto session = GetRoot<Session>(v.mv_data);
    if (session->expires_at() > now_s()) return { session->user() };
    spdlog::debug("Session {:x} is expired", session_id);
    return {};
  }

  auto ReadTxnBase::get_user_id(string_view name) -> optional<uint64_t> {
    // TODO: Handle hash collisions, maybe double hashing.
    MDB_val v;
    if (db_get(txn, db.dbis[User_Name], Cursor(name, db.seed), v)) return {};
    return { val_as<uint64_t>(v) };
  }
  auto ReadTxnBase::get_user(uint64_t id) -> optional<const User*> {
    MDB_val v;
    if (db_get(txn, db.dbis[User_User], id, v)) return {};
    return { GetRoot<User>(v.mv_data) };
  }
  auto ReadTxnBase::get_user_stats(uint64_t id) -> optional<const UserStats*> {
    MDB_val v;
    if (db_get(txn, db.dbis[UserStats_User], id, v)) return {};
    return { GetRoot<UserStats>(v.mv_data) };
  }
  auto ReadTxnBase::get_local_user(uint64_t id) -> optional<const LocalUser*> {
    MDB_val v;
    if (db_get(txn, db.dbis[LocalUser_User], id, v)) return {};
    return { GetRoot<LocalUser>(v.mv_data) };
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
      cursor ? cursor : std::optional(Cursor(board_id, 0)),
      { Cursor(board_id, ID_MAX) },
      second_key
    );
  }
  auto ReadTxnBase::user_is_subscribed(uint64_t user_id, uint64_t board_id) -> bool {
    MDB_val v;
    return !db_get(txn, db.dbis[Subscription_UserBoard], Cursor(user_id, board_id), v);
  }

  auto ReadTxnBase::get_board_id(string_view name) -> optional<uint64_t> {
    // TODO: Handle hash collisions, maybe double hashing.
    MDB_val v;
    if (db_get(txn, db.dbis[Board_Name], Cursor(name, db.seed), v)) return {};
    return { val_as<uint64_t>(v) };
  }
  auto ReadTxnBase::get_board(uint64_t id) -> optional<const Board*> {
    MDB_val v;
    if (db_get(txn, db.dbis[Board_Board], id, v)) return {};
    return { GetRoot<Board>(v.mv_data) };
  }
  auto ReadTxnBase::get_board_stats(uint64_t id) -> optional<const BoardStats*> {
    MDB_val v;
    if (db_get(txn, db.dbis[BoardStats_Board], id, v)) return {};
    return { GetRoot<BoardStats>(v.mv_data) };
  }
  auto ReadTxnBase::get_local_board(uint64_t id) -> optional<const LocalBoard*> {
    MDB_val v;
    if (db_get(txn, db.dbis[LocalBoard_Board], id, v)) return {};
    return { GetRoot<LocalBoard>(v.mv_data) };
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
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, ID_MAX) },
      second_key
    );
  }
  auto ReadTxnBase::list_created_boards(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Owner_UserBoard],
      txn,
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, ID_MAX) },
      second_key
    );
  }

  auto ReadTxnBase::get_post_stats(uint64_t id) -> optional<const PostStats*> {
    MDB_val v;
    if (db_get(txn, db.dbis[PostStats_Post], id, v)) return {};
    return { GetRoot<PostStats>(v.mv_data) };
  }
  auto ReadTxnBase::get_page(uint64_t id) -> optional<const Page*> {
    MDB_val v;
    if (db_get(txn, db.dbis[Page_Page], id, v)) return {};
    return { GetRoot<Page>(v.mv_data) };
  }
  auto ReadTxnBase::list_pages_of_board_new(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[PagesNew_BoardTimePage],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_pages_of_board_top(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[PagesTop_BoardKarmaPage],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_pages_of_user_new(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[Owner_UserPage],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX)),
      { Cursor(user_id, 0) },
      second_key
    );
  }
  auto ReadTxnBase::list_pages_of_user_top(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[PagesTop_UserKarmaPage],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX, ID_MAX)),
      { Cursor(user_id, 0, 0) }
    );
  }

  auto ReadTxnBase::get_note(uint64_t id) -> optional<const Note*> {
    MDB_val v;
    if (db_get(txn, db.dbis[Note_Note], id, v)) return {};
    return { GetRoot<Note>(v.mv_data) };
  }
  auto ReadTxnBase::list_notes_of_post_new(uint64_t post_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ChildrenNew_PostTimeNote],
      txn,
      cursor ? cursor : optional(Cursor(post_id, ID_MAX, ID_MAX)),
      { Cursor(post_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_post_top(uint64_t post_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ChildrenTop_PostKarmaNote],
      txn,
      cursor ? cursor : optional(Cursor(post_id, ID_MAX, ID_MAX)),
      { Cursor(post_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_board_new(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[NotesNew_BoardTimeNote],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_board_top(uint64_t board_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[NotesTop_BoardKarmaNote],
      txn,
      cursor ? cursor : optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_user_new(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[Owner_UserNote],
      txn,
      cursor ? cursor : optional(Cursor(user_id, ID_MAX)),
      { Cursor(user_id, 0) },
      second_key
    );
  }
  auto ReadTxnBase::list_notes_of_user_top(uint64_t user_id, OptCursor cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[NotesTop_UserKarmaNote],
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
      now + lifetime_seconds
    ));
    db_put(txn, db.dbis[Session_Session], id, fbb);
    spdlog::debug("Created session {:x} for user {:x} (IP {}, user agent {})", id, user, ip, user_agent);
    return { id, now + lifetime_seconds };
  }
  auto WriteTxn::create_user(FlatBufferBuilder& builder) -> uint64_t {
    uint64_t id = next_id();
    set_user(id, builder);
    return id;
  }
  auto WriteTxn::set_user(uint64_t id, FlatBufferBuilder& builder) -> void {
    auto user = GetRoot<User>(builder.GetBufferPointer());
    auto old_user_opt = get_user(id);
    if (old_user_opt) {
      spdlog::debug("Updating user {:x} (name {})", id, user->name()->string_view());
      auto old_user = *old_user_opt;
      if (user->name() != old_user->name()) {
        db_del(txn, db.dbis[User_Name], Cursor(old_user->name()->string_view(), db.seed));
      }
    } else {
      spdlog::debug("Creating user {:x} (name {})", id, user->name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateUserStats(fbb));
      db_put(txn, db.dbis[UserStats_User], id, fbb);
    }
    db_put(txn, db.dbis[User_User], id, builder);
    db_put(txn, db.dbis[User_Name], Cursor(user->name()->string_view(), db.seed), id);

    // TODO: Handle media (avatar)
  }
  auto WriteTxn::set_local_user(uint64_t id, FlatBufferBuilder& builder) -> void {
    db_put(txn, db.dbis[LocalUser_User], id, builder);
  }
  auto WriteTxn::delete_user(uint64_t id) -> bool {
    auto user_opt = get_user(id);
    if (!user_opt) {
      spdlog::warn("Tried to delete nonexistent user {:x}", id);
      return false;
    }

    spdlog::debug("Deleting user {:x}", id);
    db_del(txn, db.dbis[User_User], id);
    db_del(txn, db.dbis[User_Name], Cursor((*user_opt)->name()->string_view(), db.seed));
    db_del(txn, db.dbis[UserStats_User], id);
    db_del(txn, db.dbis[LocalUser_User], id);

    delete_range(txn, db.dbis[Subscription_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX),
      [&](MDB_val& k, MDB_val&) {
        Cursor c(k);
        auto board = c.int_field_1();
        auto board_stats = get_board_stats(board);
        db_del(txn, db.dbis[Subscription_BoardUser], Cursor(board, c.int_field_0()));
        if (board_stats) {
          auto s = *board_stats;
          FlatBufferBuilder fbb;
          fbb.Finish(CreateBoardStats(fbb,
            s->page_count(),
            s->note_count(),
            std::min(s->subscriber_count(), s->subscriber_count() - 1),
            s->users_active_half_year(),
            s->users_active_month(),
            s->users_active_week(),
            s->users_active_day()
          ));
          db_put(txn, db.dbis[BoardStats_Board], id, fbb);
        }
      }
    );
    delete_range(txn, db.dbis[Owner_UserPage], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Owner_UserNote], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Owner_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Owner_UserMedia], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Hide_UserPost], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Hide_UserUser], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[Hide_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[NotesTop_UserKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[Vote_UserPost], Cursor(id, 0), Cursor(id, ID_MAX),
      [&](MDB_val& k, MDB_val&) {
        Cursor c(k);
        db_del(txn, db.dbis[Vote_PostUser], Cursor(c.int_field_1(), c.int_field_0()));
      }
    );

    // TODO: Delete everything connected to the User
    // TODO: Does this delete owned posts and boards?
    return true;
  }

  auto WriteTxn::create_board(FlatBufferBuilder& builder) -> uint64_t {
    uint64_t id = next_id();
    set_board(id, builder);
    return id;
  }
  auto WriteTxn::set_board(uint64_t id, FlatBufferBuilder& builder) -> void {
    auto board = GetRoot<Board>(builder.GetBufferPointer());
    auto old_board_opt = get_board(id);
    if (old_board_opt) {
      spdlog::debug("Updating board {:x} (name {})", id, board->name()->string_view());
      auto old_board = *old_board_opt;
      if (board->name() != old_board->name()) {
        db_del(txn, db.dbis[Board_Name], Cursor(old_board->name()->string_view(), db.seed));
      }
    } else {
      spdlog::debug("Creating board {:x} (name {})", id, board->name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateBoardStats(fbb));
      db_put(txn, db.dbis[BoardStats_Board], id, fbb);
    }
    db_put(txn, db.dbis[Board_Board], id, builder);
    db_put(txn, db.dbis[Board_Name], Cursor(board->name()->string_view(), db.seed), id);

    // TODO: Handle media (avatar, banner)
  }
  auto WriteTxn::set_local_board(uint64_t id, FlatBufferBuilder& builder) -> void {
    auto board = GetRoot<LocalBoard>(builder.GetBufferPointer());
    assert_fmt(!!get_user(board->owner()), "set_local_board: board {:x} owner user {:x} does not exist", id, board->owner());
    auto old_board_opt = get_local_board(id);
    if (old_board_opt) {
      spdlog::debug("Updating local board {:x}", id);
      auto old_board = *old_board_opt;
      if (board->owner() != old_board->owner()) {
        spdlog::info("Changing owner of local board {:x}: {:x} -> {:x}", id, old_board->owner(), board->owner());
        db_del(txn, db.dbis[Owner_UserBoard], Cursor(old_board->owner(), id));
      }
    } else {
      spdlog::debug("Updating local board {:x}", id);
    }
    db_put(txn, db.dbis[Owner_UserBoard], Cursor(board->owner(), id), board->owner());
    db_put(txn, db.dbis[LocalBoard_Board], id, builder);
  }
  auto WriteTxn::delete_board(uint64_t id) -> bool {
    auto board_opt = get_board(id);
    if (!board_opt) {
      spdlog::warn("Tried to delete nonexistent board {:x}", id);
      return false;
    }

    spdlog::debug("Deleting board {:x}", id);
    MDB_val id_val{ sizeof(uint64_t), &id };
    db_del(txn, db.dbis[Board_Board], id_val);
    db_del(txn, db.dbis[Board_Name], Cursor((*board_opt)->name()->string_view(), db.seed));
    db_del(txn, db.dbis[BoardStats_Board], id_val);
    db_del(txn, db.dbis[LocalBoard_Board], id_val);
    // TODO: Owner_UserBoard

    delete_range(txn, db.dbis[Subscription_BoardUser], Cursor(id, 0), Cursor(id, ID_MAX),
      [&](MDB_val& k, MDB_val&) {
        Cursor c(k);
        db_del(txn, db.dbis[Subscription_UserBoard], Cursor(c.int_field_1(), c.int_field_0()));
      }
    );
    delete_range(txn, db.dbis[PagesNew_BoardTimePage], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[NotesNew_BoardTimeNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    return true;
  }
  auto WriteTxn::set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
    MDB_val v;
    bool existing = !db_get(txn, db.dbis[Subscription_BoardUser], Cursor(board_id, user_id), v);
    auto board_stats = get_board_stats(board_id);
    auto subscriber_count = board_stats ? (*board_stats)->subscriber_count() : 0;
    if (subscribed) {
      assert_fmt(!!get_user(user_id), "set_subscription: user {:x} does not exist", user_id);
      assert_fmt(!!board_stats, "set_subscription: board {:x} does not exist", board_id);
      if (!existing) {
        spdlog::debug("Subscribing user {:x} to board {:x}", user_id, board_id);
        auto now = now_s();
        db_put(txn, db.dbis[Subscription_BoardUser], Cursor(board_id, user_id), now);
        db_put(txn, db.dbis[Subscription_UserBoard], Cursor(user_id, board_id), now);
        subscriber_count++;
      }
    } else if (existing) {
      spdlog::debug("Unsubscribing user {:x} from board {:x}", user_id, board_id);
      db_del(txn, db.dbis[Subscription_BoardUser], Cursor(board_id, user_id));
      db_del(txn, db.dbis[Subscription_UserBoard], Cursor(user_id, board_id));
      subscriber_count = std::min(subscriber_count, subscriber_count - 1);
    }
    if (board_stats) {
      auto s = *board_stats;
      FlatBufferBuilder fbb;
      fbb.Finish(CreateBoardStats(fbb,
        s->page_count(),
        s->note_count(),
        subscriber_count,
        s->users_active_half_year(),
        s->users_active_month(),
        s->users_active_week(),
        s->users_active_day()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board_id, fbb);
    }
  }

  auto WriteTxn::create_page(FlatBufferBuilder& builder) -> uint64_t {
    uint64_t id = next_id();
    set_page(id, builder);
    return id;
  }
  static inline auto karma_uint(int64_t karma) -> uint64_t {
    if (karma < 0) return (uint64_t)(std::numeric_limits<int64_t>::max() + karma);
    return (uint64_t)std::numeric_limits<int64_t>::max() + (uint64_t)karma;
  }
  auto WriteTxn::set_page(uint64_t id, FlatBufferBuilder& builder) -> void {
    auto page = GetRoot<Page>(builder.GetBufferPointer());
    auto old_page_opt = get_page(id);
    FlatBufferBuilder fbb;
    int64_t karma = 0;
    if (old_page_opt) {
      spdlog::debug("Updating top-level post {:x} (board {:x}, author {:x})", id, page->board(), page->author());
      auto stats_opt = get_post_stats(id);
      assert_fmt(!!stats_opt, "set_page: post_stats not in database for existing page {:x}", id);
      karma = (*stats_opt)->karma();
      auto old_page = *old_page_opt;
      assert_fmt(page->author() == old_page->author(), "set_page: cannot change author of page {:x}", id);
      assert_fmt(page->created_at() == old_page->created_at(), "set_page: cannot change created_at of page {:x}", id);
      if (page->board() != old_page->board()) {
        db_del(txn, db.dbis[PagesNew_BoardTimePage], Cursor(old_page->board(), page->created_at(), id));
        db_del(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(old_page->board(), karma_uint(karma), id));
        if (auto board_stats = get_board_stats(old_page->board())) {
          auto s = *board_stats;
          fbb.Finish(CreateBoardStats(fbb,
            std::min(s->page_count(), s->page_count() - 1),
            s->note_count(),
            s->subscriber_count(),
            s->users_active_half_year(),
            s->users_active_month(),
            s->users_active_week(),
            s->users_active_day()
          ));
          db_put(txn, db.dbis[BoardStats_Board], old_page->board(), fbb);
        }
      }
    } else {
      spdlog::debug("Creating top-level post {:x} (board {:x}, author {:x})", id, page->board(), page->author());
      db_put(txn, db.dbis[Owner_UserPage], Cursor(page->author(), id), page->author());
      db_put(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(page->author(), 1, id), id);
      db_put(txn, db.dbis[PagesNew_BoardTimePage], Cursor(page->board(), page->created_at(), id), id);
      db_put(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma_uint(karma), id), id);
      db_put(txn, db.dbis[PagesNewComments_BoardTimePage], Cursor(page->board(), page->created_at(), id), id);
      db_put(txn, db.dbis[PagesMostComments_BoardCommentsPage], Cursor(page->board(), 0, id), id);
      fbb.ForceDefaults(true);
      fbb.Finish(CreatePostStats(fbb, page->created_at()));
      db_put(txn, db.dbis[PostStats_Post], id, fbb);
      if (auto user_stats = get_user_stats(page->author())) {
        auto s = *user_stats;
        fbb.Clear();
        fbb.Finish(CreateUserStats(fbb,
          s->note_count(),
          s->note_karma(),
          s->page_count() + 1,
          s->page_karma()
        ));
        db_put(txn, db.dbis[UserStats_User], page->author(), fbb);
      }
      if (auto board_stats = get_board_stats(page->board())) {
        auto s = *board_stats;
        fbb.Clear();
        fbb.Finish(CreateBoardStats(fbb,
          s->page_count() + 1,
          s->note_count(),
          s->subscriber_count(),
          s->users_active_half_year(),
          s->users_active_month(),
          s->users_active_week(),
          s->users_active_day()
        ));
        db_put(txn, db.dbis[BoardStats_Board], page->board(), fbb);
      }
    }
    db_put(txn, db.dbis[Page_Page], id, builder);
  }
  auto WriteTxn::delete_child_note(uint64_t id, uint64_t board_id) -> uint64_t {
    const auto note_opt = get_note(id);
    const auto stats_opt = get_post_stats(id);
    if (!note_opt || !stats_opt) {
      spdlog::warn("Tried to delete nonexistent comment {:x}", id);
      return 0;
    }
    const auto note = *note_opt;
    const auto stats = *stats_opt;
    const auto karma = stats->karma();
    const auto latest_comment = stats->latest_comment(),
      descendant_count = stats->descendant_count(),
      author = note->author(),
      created_at = note->created_at(),
      parent = note->parent();

    spdlog::debug("Deleting comment {:x} (parent {:x}, author {:x}, board {:x})", id, parent, author, board_id);
    if (auto user_stats = get_user_stats(author)) {
      auto s = *user_stats;
      FlatBufferBuilder fbb;
      fbb.Finish(CreateUserStats(fbb,
        std::min(s->note_count(), s->note_count() - 1),
        karma > 0 ? std::min(s->note_karma(), s->note_karma() - karma) : s->note_karma() - karma,
        s->page_count(),
        s->page_karma()
      ));
      db_put(txn, db.dbis[UserStats_User], note->author(), fbb);
    }
    db_del(txn, db.dbis[NotesNew_BoardTimeNote], Cursor(board_id, created_at, id));
    db_del(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(board_id, karma_uint(karma), id));
    db_del(txn, db.dbis[NotesNewComments_BoardTimeNote], Cursor(board_id, latest_comment, id));
    db_del(txn, db.dbis[NotesMostComments_BoardCommentsNote], Cursor(board_id, descendant_count, id));

    delete_range(txn, db.dbis[Vote_PostUser], Cursor(id, 0), Cursor(id, ID_MAX),
      [&](MDB_val& k, MDB_val&) {
        Cursor c(k);
        db_del(txn, db.dbis[Vote_UserPost], Cursor(c.int_field_1(), c.int_field_0()));
      }
    );
    std::set<uint64_t> children;
    delete_range(txn, db.dbis[ChildrenNew_PostTimeNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX),
      [&children](MDB_val&, MDB_val& v) {
        children.insert(val_as<uint64_t>(v));
      }
    );
    delete_range(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    db_del(txn, db.dbis[Note_Note], id);
    db_del(txn, db.dbis[PostStats_Post], id);
    db_del(txn, db.dbis[Owner_UserNote], Cursor(author, id));
    db_del(txn, db.dbis[ChildrenNew_PostTimeNote], Cursor(parent, created_at, id));
    db_del(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(parent, karma_uint(karma), id));

    uint64_t n = 0;
    for (uint64_t child : children) {
      assert(child != id);
      n += delete_child_note(child, board_id);
    }
    return n;
  }
  auto WriteTxn::delete_page(uint64_t id) -> bool {
    const auto page_opt = get_page(id);
    const auto stats_opt = get_post_stats(id);
    if (!page_opt || !stats_opt) {
      spdlog::warn("Tried to delete nonexistent top-level post {:x}", id);
      return false;
    }
    const auto page = *page_opt;
    const auto stats = *stats_opt;
    const auto karma = stats->karma();
    const auto author = page->author(),
      board = page->board(),
      created_at = page->created_at(),
      latest_comment = stats->latest_comment(),
      descendant_count = stats->descendant_count();

    spdlog::debug("Deleting top-level post {:x} (board {:x}, author {:x})", id, board, author);
    FlatBufferBuilder fbb;
    if (auto user_stats = get_user_stats(author)) {
      auto s = *user_stats;
      fbb.Finish(CreateUserStats(fbb,
        s->note_count(),
        s->note_karma(),
        std::min(s->page_count(), s->page_count() - 1),
        karma > 0 ? std::min(s->page_karma(), s->page_karma() - karma) : s->page_karma() - karma
      ));
      db_put(txn, db.dbis[UserStats_User], author, fbb);
      fbb.Clear();
    }
    if (auto board_stats = get_board_stats(board)) {
      auto s = *board_stats;
      fbb.Finish(CreateBoardStats(fbb,
        std::min(s->page_count(), s->page_count() - 1),
        std::min(s->note_count(), s->note_count() - descendant_count),
        s->subscriber_count(),
        s->users_active_half_year(),
        s->users_active_month(),
        s->users_active_week(),
        s->users_active_day()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board, fbb);
      fbb.Clear();
    }

    delete_range(txn, db.dbis[Vote_PostUser], Cursor(id, 0), Cursor(id, ID_MAX),
      [&](MDB_val& k, MDB_val&) {
        Cursor c(k);
        db_del(txn, db.dbis[Vote_UserPost], Cursor(c.int_field_1(), c.int_field_0()));
      }
    );
    std::set<uint64_t> children;
    delete_range(txn, db.dbis[ChildrenNew_PostTimeNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX),
      [&children](MDB_val&, MDB_val& v) {
        children.insert(val_as<uint64_t>(v));
      }
    );
    delete_range(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    db_del(txn, db.dbis[Page_Page], id);
    db_del(txn, db.dbis[PostStats_Post], id);
    db_del(txn, db.dbis[Owner_UserPage], Cursor(author, id));
    db_del(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(author, karma_uint(karma), id));
    db_del(txn, db.dbis[PagesNew_BoardTimePage], Cursor(board, created_at, id));
    db_del(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(board, karma_uint(karma), id));
    db_del(txn, db.dbis[PagesNewComments_BoardTimePage], Cursor(board, latest_comment, id));
    db_del(txn, db.dbis[PagesMostComments_BoardCommentsPage], Cursor(board, descendant_count, id));

    for (uint64_t child : children) delete_child_note(child, board);

    return true;
  }

  auto WriteTxn::create_note(FlatBufferBuilder& builder) -> uint64_t {
    uint64_t id = next_id();
    set_note(id, builder);
    return id;
  }
  auto WriteTxn::set_note(uint64_t id, FlatBufferBuilder& builder) -> void {
    const auto note = GetRoot<Note>(builder.GetBufferPointer());
    const auto old_note_opt = get_note(id);
    const auto stats_opt = get_post_stats(id);
    const auto page_opt = get_page(note->page());
    int64_t karma = 0;
    assert_fmt(!!page_opt, "set_note: note {:x} top-level ancestor page {:x} does not exist", id, note->page());
    const auto page = *page_opt;
    const auto author = note->author(), board = page->board();
    if (old_note_opt) {
      spdlog::debug("Updating comment {:x} (parent {:x}, author {:x})", id, note->parent(), note->author());
      assert(!!stats_opt);
      const auto old_note = *old_note_opt;
      const auto stats = *stats_opt;
      karma = stats->karma();
      assert(note->author() == old_note->author());
      assert(note->parent() == old_note->parent());
      assert(note->page() == old_note->page());
      assert(note->created_at() == old_note->created_at());
    } else {
      spdlog::debug("Creating comment {:x} (parent {:x}, author {:x})", id, note->parent(), note->author());
      db_put(txn, db.dbis[Owner_UserNote], Cursor(author, id), author);
      db_put(txn, db.dbis[NotesTop_UserKarmaNote], Cursor(author, karma_uint(karma), id), id);
      db_put(txn, db.dbis[NotesNew_BoardTimeNote], Cursor(board, note->created_at(), id), id);
      db_put(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(board, karma_uint(karma), id), id);
      db_put(txn, db.dbis[ChildrenNew_PostTimeNote], Cursor(note->parent(), note->created_at(), id), id);
      db_put(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(karma), id), id);
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreatePostStats(fbb, note->created_at()));
      db_put(txn, db.dbis[PostStats_Post], id, fbb);

      for (optional<const Note*> note_opt = {note}; note_opt; note_opt = get_note((*note_opt)->parent())) {
        const auto parent = (*note_opt)->parent();
        if (const auto parent_stats_opt = get_post_stats(parent)) {
          uint64_t parent_created_at;
          if (auto parent_opt = get_note(parent)) parent_created_at = (*parent_opt)->created_at();
          else if (parent == note->page()) parent_created_at = page->created_at();
          else continue;
          const auto s = *parent_stats_opt;
          const bool is_active = note->created_at() >= parent_created_at &&
              note->created_at() - parent_created_at <= ACTIVE_COMMENT_MAX_AGE,
            is_newer = is_active && note->created_at() > s->latest_comment();
          const auto last_latest_comment = s->latest_comment(),
            last_descendant_count = s->descendant_count();
          fbb.Clear();
          fbb.Finish(CreatePostStats(fbb,
            is_newer ? note->created_at() : s->latest_comment(),
            is_active ? s->latest_comment_necro() : std::max(s->latest_comment_necro(), note->created_at()),
            s->descendant_count() + 1,
            s->child_count() + 1,
            s->upvotes(),
            s->downvotes(),
            s->karma()
          ));
          db_put(txn, db.dbis[PostStats_Post], parent, fbb);
          if (parent == note->page()) {
            if (is_newer) {
              db_del(txn, db.dbis[PagesNewComments_BoardTimePage], Cursor(board, last_latest_comment, parent));
              db_put(txn, db.dbis[PagesNewComments_BoardTimePage], Cursor(board, note->created_at(), parent), parent);
            }
            db_del(txn, db.dbis[PagesMostComments_BoardCommentsPage], Cursor(board, last_descendant_count, parent));
            db_put(txn, db.dbis[PagesMostComments_BoardCommentsPage], Cursor(board, last_descendant_count + 1, parent), parent);
          } else {
            if (is_newer) {
              db_del(txn, db.dbis[NotesNewComments_BoardTimeNote], Cursor(board, last_latest_comment, parent));
              db_put(txn, db.dbis[NotesNewComments_BoardTimeNote], Cursor(board, note->created_at(), parent), parent);
            }
            db_del(txn, db.dbis[NotesMostComments_BoardCommentsNote], Cursor(board, last_descendant_count, parent));
            db_put(txn, db.dbis[NotesMostComments_BoardCommentsNote], Cursor(board, last_descendant_count + 1, parent), parent);
          }
        }
      }
      if (auto user_stats = get_user_stats(author)) {
        auto s = *user_stats;
        fbb.Clear();
        fbb.Finish(CreateUserStats(fbb,
          s->note_count() + 1,
          s->note_karma(),
          s->page_count(),
          s->page_karma()
        ));
        db_put(txn, db.dbis[UserStats_User], author, fbb);
      }
      if (auto board_stats = get_board_stats(board)) {
        auto s = *board_stats;
        fbb.Clear();
        fbb.Finish(CreateBoardStats(fbb,
          s->page_count(),
          s->note_count() + 1,
          s->subscriber_count(),
          s->users_active_half_year(),
          s->users_active_month(),
          s->users_active_week(),
          s->users_active_day()
        ));
        db_put(txn, db.dbis[BoardStats_Board], board, fbb);
      }
    }
    db_put(txn, db.dbis[Note_Note], id, builder);
  }
  auto WriteTxn::delete_note(uint64_t id) -> uint64_t {
    const auto note_opt = get_note(id);
    const auto stats_opt = get_post_stats(id);
    if (!note_opt || !stats_opt) {
      spdlog::warn("Tried to delete nonexistent comment {:x}", id);
      return false;
    }
    const auto note = *note_opt;
    const auto stats = *stats_opt;
    const auto page_id = note->page();
    const auto page_opt = get_page(page_id);
    assert_fmt(!!page_opt, "delete_note: note {:x} top-level ancestor page {:x} does not exist", id, page_id);
    const auto board_id = (*page_opt)->board(),
      descendant_count = stats->descendant_count();

    FlatBufferBuilder fbb;
    for (optional<const Note*> note_opt = {note}; note_opt; note_opt = get_note((*note_opt)->parent())) {
      const auto parent = (*note_opt)->parent();
      if (const auto parent_stats_opt = get_post_stats(parent)) {
        const auto s = *parent_stats_opt;
        const auto last_descendant_count = s->descendant_count(),
          next_descendant_count =
            (descendant_count + 1) > s->descendant_count() ? 0 : s->descendant_count() - (descendant_count + 1);
        fbb.Clear();
        fbb.Finish(CreatePostStats(fbb,
          s->latest_comment(),
          s->latest_comment_necro(),
          next_descendant_count,
          parent == note->parent()
            ? std::min(s->child_count(), s->child_count() - 1)
            : s->child_count(),
          s->upvotes(),
          s->downvotes(),
          s->karma()
        ));
        db_put(txn, db.dbis[PostStats_Post], parent, fbb);
        if (parent == note->page()) {
          db_del(txn, db.dbis[PagesMostComments_BoardCommentsPage], Cursor(board_id, last_descendant_count, parent));
          db_put(txn, db.dbis[PagesMostComments_BoardCommentsPage], Cursor(board_id, next_descendant_count, parent), parent);
        } else {
          db_del(txn, db.dbis[NotesMostComments_BoardCommentsNote], Cursor(board_id, last_descendant_count, parent));
          db_put(txn, db.dbis[NotesMostComments_BoardCommentsNote], Cursor(board_id, next_descendant_count, parent), parent);
        }
      }
    }
    if (const auto board_stats_opt = get_board_stats(board_id)) {
      const auto s = *board_stats_opt;
      fbb.Clear();
      fbb.Finish(CreateBoardStats(fbb,
        s->page_count(),
        (descendant_count + 1) > s->note_count() ? 0 : s->note_count() - (descendant_count + 1),
        s->subscriber_count(),
        s->users_active_half_year(),
        s->users_active_month(),
        s->users_active_week(),
        s->users_active_day()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board_id, fbb);
    }

    return delete_child_note(id, board_id);
  }

  auto WriteTxn::set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    const auto existing = get_vote_of_user_for_post(user_id, post_id);
    const int64_t diff = vote - existing;
    if (!diff) return;
    const auto page_opt = get_page(post_id);
    const auto note_opt = page_opt ? std::nullopt : get_note(post_id);
    assert(page_opt || note_opt);
    const auto op_id = page_opt ? (*page_opt)->author() : (*note_opt)->author();
    const auto op = get_user(op_id);
    assert(!!op);
    spdlog::debug("Setting vote from user {:x} on post {:x} to {}", user_id, post_id, (int8_t)vote);
    if (vote) {
      int8_t vote_byte = (int8_t)vote;
      MDB_val v { sizeof(int8_t), &vote_byte };
      db_put(txn, db.dbis[Vote_UserPost], Cursor(user_id, post_id), v);
      db_put(txn, db.dbis[Vote_PostUser], Cursor(post_id, user_id), v);
    } else {
      db_del(txn, db.dbis[Vote_UserPost], Cursor(user_id, post_id));
      db_del(txn, db.dbis[Vote_PostUser], Cursor(post_id, user_id));
    }
    const auto stats_opt = get_post_stats(post_id);
    const auto op_stats_opt = get_user_stats(op_id);
    assert(!!op_stats_opt);
    int64_t old_karma, new_karma;
    FlatBufferBuilder fbb;
    {
      const auto s = *stats_opt;
      old_karma = s->karma(), new_karma = old_karma + diff;
      fbb.Finish(CreatePostStats(fbb,
        s->latest_comment(),
        s->latest_comment_necro(),
        s->descendant_count(),
        s->child_count(),
        vote > 0 ? s->upvotes() + 1 : (existing > 0 ? std::min(s->upvotes(), s->upvotes() - 1) : s->upvotes()),
        vote < 0 ? s->downvotes() + 1 : (existing < 0 ? std::min(s->downvotes(), s->downvotes() - 1) : s->downvotes()),
        new_karma
      ));
      db_put(txn, db.dbis[PostStats_Post], post_id, fbb);
    }
    {
      const auto s = *op_stats_opt;
      fbb.Clear();
      fbb.Finish(CreateUserStats(fbb,
        s->note_count(),
        s->note_karma() + (page_opt ? 0 : diff),
        s->page_count(),
        s->page_karma() + (page_opt ? diff : 0)
      ));
      db_put(txn, db.dbis[UserStats_User], op_id, fbb);
    }
    if (page_opt) {
      const auto page = *page_opt;
      db_del(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma_uint(old_karma), post_id));
      db_del(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(page->author(), karma_uint(old_karma), post_id));
      db_put(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma_uint(new_karma), post_id), post_id);
      db_put(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(page->author(), karma_uint(new_karma), post_id), post_id);
    } else {
      const auto note = *note_opt;
      const auto note_page_opt = get_page(note->page());
      assert(!!note_page_opt);
      const auto note_page = *note_page_opt;
      db_del(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(note_page->board(), karma_uint(old_karma), post_id));
      db_del(txn, db.dbis[NotesTop_UserKarmaNote], Cursor(note->author(), karma_uint(old_karma), post_id));
      db_del(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(old_karma), post_id));
      db_put(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(note_page->board(), karma_uint(new_karma), post_id), post_id);
      db_put(txn, db.dbis[NotesTop_UserKarmaNote], Cursor(note->author(), karma_uint(new_karma), post_id), post_id);
      db_put(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(new_karma), post_id), post_id);
    }
  }
}
