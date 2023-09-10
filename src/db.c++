#include <random>
#include <vector>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <duthomhas/csprng.hpp>
#include "db.h++"

using std::optional, flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot, flatbuffers::Offset;

#define assert_fmt(CONDITION, ...) if (!(CONDITION)) { spdlog::critical(__VA_ARGS__); throw std::runtime_error("Assertion failed: " #CONDITION); }

namespace Ludwig {
  static constexpr uint64_t ACTIVE_COMMENT_MAX_AGE = 86400 * 2; // 2 days

  enum Dbi {
    Settings,

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
    Subscription_UserBoard,
    Owner_UserMedia,
    Owner_UserUrl,

    Board_Board,
    Board_Name,
    BoardStats_Board,
    LocalBoard_Board,
    PagesTop_BoardKarmaPage,
    PagesNew_BoardTimePage,
    NotesTop_BoardKarmaNote,
    NotesNew_BoardTimeNote,
    Subscription_BoardUser,

    Page_Page,
    PageStats_Page,
    Note_Note,
    NoteStats_Note,
    ChildrenNew_PostTimeNote,
    ChildrenTop_PostKarmaNote,
    Contains_PostMedia,

    Vote_UserPost,
    Vote_PostUser,

    LinkCard_Url,
    Contains_UrlPage,

    Media_Media,
    Refcount_Media,
    Contains_MediaPost,
    LinkCardContains_MediaUrl
  };

  inline auto db_get(MDB_txn* txn, MDB_dbi dbi, MDB_val& k, MDB_val& v) -> int {
    return mdb_get(txn, dbi, &k, &v);
  }

  inline auto db_get(MDB_txn* txn, MDB_dbi dbi, std::string_view k, MDB_val& v) -> int {
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
    if (auto err = mdb_put(txn, dbi, &k, &v, flags)) {
      throw DBError("Write failed", err);
    }
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, std::string_view k, MDB_val& v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    db_put(txn, dbi, kval, v, flags);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, std::string_view k, std::string_view v, unsigned flags = 0) -> void {
    MDB_val kval { k.length(), const_cast<char*>(k.data()) };
    MDB_val vval { v.length(), const_cast<char*>(v.data()) };
    db_put(txn, dbi, kval, vval, flags);
  }

  inline auto db_put(MDB_txn* txn, MDB_dbi dbi, std::string_view k, uint64_t v, unsigned flags = 0) -> void {
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

  DB::DB(const char* filename, size_t map_size_mb) :
    map_size(map_size_mb * 1024 * 1024 - (map_size_mb * 1024 * 2014) % (size_t)sysconf(_SC_PAGESIZE)) {
    int err;
    MDB_txn* txn = nullptr;
    if ((err =
      mdb_env_create(&env) ||
      mdb_env_set_maxdbs(env, 128) ||
      mdb_env_set_mapsize(env, map_size) ||
      mdb_env_open(env, filename, MDB_NOSUBDIR | MDB_NOSYNC, 0600) ||
      mdb_txn_begin(env, nullptr, 0, &txn)
    )) goto die;
#   define MK_DBI(NAME, FLAGS) if ((err = mdb_dbi_open(txn, #NAME, FLAGS | MDB_CREATE, dbis + NAME))) goto die;
    MK_DBI(Settings, 0)

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
    MK_DBI(Subscription_UserBoard, 0)
    MK_DBI(Owner_UserMedia, 0)
    MK_DBI(Owner_UserUrl, 0)

    MK_DBI(Board_Board, MDB_INTEGERKEY)
    MK_DBI(Board_Name, MDB_INTEGERKEY)
    MK_DBI(BoardStats_Board, MDB_INTEGERKEY)
    MK_DBI(LocalBoard_Board, MDB_INTEGERKEY)
    MK_DBI(PagesTop_BoardKarmaPage, 0)
    MK_DBI(PagesNew_BoardTimePage, 0)
    MK_DBI(NotesTop_BoardKarmaNote, 0)
    MK_DBI(NotesNew_BoardTimeNote, 0)
    MK_DBI(Subscription_BoardUser, 0)

    MK_DBI(Page_Page, MDB_INTEGERKEY)
    MK_DBI(PageStats_Page, MDB_INTEGERKEY)
    MK_DBI(Note_Note, MDB_INTEGERKEY)
    MK_DBI(NoteStats_Note, MDB_INTEGERKEY)
    MK_DBI(ChildrenNew_PostTimeNote, 0)
    MK_DBI(ChildrenTop_PostKarmaNote, 0)
    MK_DBI(Contains_PostMedia, 0)

    MK_DBI(Vote_UserPost, 0)
    MK_DBI(Vote_PostUser, 0)

    MK_DBI(LinkCard_Url, MDB_INTEGERKEY)
    MK_DBI(Contains_UrlPage, 0)

    MK_DBI(Media_Media, MDB_INTEGERKEY)
    MK_DBI(Refcount_Media, MDB_INTEGERKEY)
    MK_DBI(Contains_MediaPost, 0)
    MK_DBI(LinkCardContains_MediaUrl, 0)

#   undef MK_DBI

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
      db_put(txn, dbis[Settings], SettingsKey::nsfw_allowed, 1ULL);
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
    throw DBError("Failed to open database", err);
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

  auto ReadTxnBase::get_setting_str(std::string_view key) -> std::string_view {
    MDB_val v;
    if (db_get(txn, db.dbis[Settings], key, v)) return {};
    return std::string_view(static_cast<const char*>(v.mv_data), v.mv_size);
  }
  auto ReadTxnBase::get_setting_int(std::string_view key) -> uint64_t {
    MDB_val v;
    if (db_get(txn, db.dbis[Settings], key, v)) return {};
    return val_as<uint64_t>(v);
  }

  auto ReadTxnBase::get_user_id(std::string_view name) -> optional<uint64_t> {
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

  auto ReadTxnBase::get_board_id(std::string_view name) -> optional<uint64_t> {
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

  auto ReadTxnBase::get_page(uint64_t id) -> optional<const Page*> {
    MDB_val v;
    if (db_get(txn, db.dbis[Page_Page], id, v)) return {};
    return { GetRoot<Page>(v.mv_data) };
  }
  auto ReadTxnBase::get_page_stats(uint64_t id) -> optional<const PageStats*> {
    MDB_val v;
    if (db_get(txn, db.dbis[PageStats_Page], id, v)) return {};
    return { GetRoot<PageStats>(v.mv_data) };
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
  auto ReadTxnBase::get_note_stats(uint64_t id) -> optional<const NoteStats*> {
    MDB_val v;
    if (db_get(txn, db.dbis[NoteStats_Note], id, v)) return {};
    return { GetRoot<NoteStats>(v.mv_data) };
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
    MDB_cursor* cur;
    mdb_cursor_open(txn, dbi, &cur);
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
  auto WriteTxn::create_user(FlatBufferBuilder&& builder, Offset<User> offset) -> uint64_t {
    uint64_t id = next_id();
    set_user(id, std::move(builder), offset);
    return id;
  }
  auto WriteTxn::set_user(uint64_t id, FlatBufferBuilder&& builder, Offset<User> offset) -> void {
    builder.Finish(offset);
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
  auto WriteTxn::set_local_user(uint64_t id, FlatBufferBuilder&& builder, Offset<LocalUser> offset) -> void {
    builder.Finish(offset);
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
            s->created_at(),
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
    delete_range(txn, db.dbis[Owner_UserUrl], Cursor(id, 0), Cursor(id, ID_MAX));
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

  auto WriteTxn::create_board(FlatBufferBuilder&& builder, Offset<Board> offset) -> uint64_t {
    uint64_t id = next_id();
    set_board(id, std::move(builder), offset);
    return id;
  }
  auto WriteTxn::set_board(uint64_t id, FlatBufferBuilder&& builder, Offset<Board> offset) -> void {
    builder.Finish(offset);
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
      fbb.Finish(CreateBoardStats(fbb, board->created_at()));
      db_put(txn, db.dbis[BoardStats_Board], id, fbb);
    }
    db_put(txn, db.dbis[Board_Board], id, builder);
    db_put(txn, db.dbis[Board_Name], Cursor(board->name()->string_view(), db.seed), id);

    // TODO: Handle media (avatar, banner)
  }
  auto WriteTxn::set_local_board(uint64_t id, FlatBufferBuilder&& builder, Offset<LocalBoard> offset) -> void {
    builder.Finish(offset);
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
        s->created_at(),
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

  auto WriteTxn::create_page(FlatBufferBuilder&& builder, Offset<Page> offset) -> uint64_t {
    uint64_t id = next_id();
    set_page(id, std::move(builder), offset);
    return id;
  }
  static inline auto karma_uint(int64_t karma) -> uint64_t {
    if (karma < 0) return (uint64_t)(std::numeric_limits<int64_t>::max() + karma);
    return (uint64_t)std::numeric_limits<int64_t>::max() + (uint64_t)karma;
  }
  auto WriteTxn::set_page(uint64_t id, FlatBufferBuilder&& builder, Offset<Page> offset) -> void {
    builder.Finish(offset);
    auto page = GetRoot<Page>(builder.GetBufferPointer());
    auto old_page_opt = get_page(id);
    int64_t karma = 0;
    if (old_page_opt) {
      spdlog::debug("Updating top-level post {:x} (board {:x}, author {:x})", id, page->board(), page->author());
      auto page_stats_opt = get_page_stats(id);
      assert_fmt(!!page_stats_opt, "set_page: page stats not in database for existing page {:x}", id);
      karma = (*page_stats_opt)->karma();
      auto old_page = *old_page_opt;
      assert_fmt(page->author() == old_page->author(), "set_page: cannot change author of page {:x}", id);
      assert_fmt(page->created_at() == old_page->created_at(), "set_page: cannot change created_at of page {:x}", id);
      if (page->board() != old_page->board()) {
        db_del(txn, db.dbis[PagesNew_BoardTimePage], Cursor(old_page->board(), page->created_at(), id));
        db_del(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(old_page->board(), karma_uint(karma), id));
        if (auto board_stats = get_board_stats(old_page->board())) {
          auto s = *board_stats;
          FlatBufferBuilder fbb;
          fbb.Finish(CreateBoardStats(fbb,
            s->created_at(),
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
      {
        FlatBufferBuilder fbb;
        fbb.ForceDefaults(true);
        fbb.Finish(CreatePageStats(fbb, page->created_at()));
        db_put(txn, db.dbis[PageStats_Page], id, fbb);
      }
      if (auto user_stats = get_user_stats(page->author())) {
        auto s = *user_stats;
        FlatBufferBuilder fbb;
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
        FlatBufferBuilder fbb;
        fbb.Finish(CreateBoardStats(fbb,
          s->created_at(),
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
  auto WriteTxn::delete_note_for_page(
    uint64_t id,
    uint64_t board_id,
    optional<PageStats*> page_stats,
    optional<BoardStats*> board_stats
  ) -> bool {
    const auto note_opt = get_note(id);
    const auto note_stats = get_note_stats(id);
    if (!note_opt || !note_stats) {
      spdlog::warn("Tried to delete nonexistent comment {:x}", id);
      return false;
    }
    const auto note = *note_opt;
    const auto karma = (*note_stats)->karma();
    const auto author = note->author();
    const auto created_at = note->created_at();
    const auto parent = note->parent();

    spdlog::debug("Deleting comment {:x} (parent {:x}, author {:x}, board {:x})", id, parent, author, board_id);
    if (page_stats) {
      auto s = *page_stats;
      s->mutate_descendant_count(
        std::min(s->descendant_count(), s->descendant_count() - 1)
      );
    }
    if (board_stats) {
      auto s = *board_stats;
      s->mutate_note_count(
        std::min(s->note_count(), s->note_count() - 1)
      );
    }
    if (auto user_stats = get_user_stats(note->author())) {
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
    db_del(txn, db.dbis[NoteStats_Note], id);
    db_del(txn, db.dbis[Owner_UserNote], Cursor(author, id));
    db_del(txn, db.dbis[ChildrenNew_PostTimeNote], Cursor(parent, created_at, id));
    db_del(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(parent, karma_uint(karma), id));

    for (uint64_t child : children) {
      assert(child != id);
      delete_note_for_page(child, board_id, page_stats, board_stats);
    }

    return true;
  }
  auto WriteTxn::delete_page(uint64_t id) -> bool {
    const auto page_opt = get_page(id);
    const auto page_stats_opt = get_page_stats(id);
    if (!page_opt || !page_stats_opt) {
      spdlog::warn("Tried to delete nonexistent top-level post {:x}", id);
      return false;
    }
    const auto page = *page_opt;
    const auto page_stats = *page_stats_opt;
    const auto author = page->author();
    const auto board = page->board();
    const auto karma = page_stats->karma();
    const auto created_at = page->created_at();

    spdlog::debug("Deleting top-level post {:x} (board {:x}, author {:x})", id, board, author);
    if (auto user_stats = get_user_stats(page->author())) {
      auto s = *user_stats;
      FlatBufferBuilder fbb;
      fbb.Finish(CreateUserStats(fbb,
        s->note_count(),
        s->note_karma(),
        std::min(s->page_count(), s->page_count() - 1),
        karma > 0 ? std::min(s->page_karma(), s->page_karma() - karma) : s->page_karma() - karma
      ));
      db_put(txn, db.dbis[UserStats_User], page->author(), fbb);
    }
    FlatBufferBuilder board_stats_fbb;
    optional<BoardStats*> board_stats = {};
    if (auto board_stats_opt = get_board_stats(page->board())) {
      auto s = *board_stats_opt;
      board_stats_fbb.Finish(CreateBoardStats(board_stats_fbb,
        s->created_at(),
        std::min(s->page_count(), s->page_count() - 1),
        s->note_count(),
        s->subscriber_count(),
        s->users_active_half_year(),
        s->users_active_month(),
        s->users_active_week(),
        s->users_active_day()
      ));
      board_stats = { flatbuffers::GetMutableRoot<BoardStats>(board_stats_fbb.GetBufferPointer()) };
    }

    MDB_val id_val{ sizeof(uint64_t), &id };

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

    db_del(txn, db.dbis[Page_Page], id_val);
    db_del(txn, db.dbis[PageStats_Page], id_val);
    db_del(txn, db.dbis[Owner_UserPage], Cursor(author, id));
    db_del(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(author, karma_uint(karma), id));
    db_del(txn, db.dbis[PagesNew_BoardTimePage], Cursor(board, created_at, id));
    db_del(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(board, karma_uint(karma), id));

    for (uint64_t child : children) {
      delete_note_for_page(child, board, {}, board_stats);
    }
    if (board_stats) db_put(txn, db.dbis[BoardStats_Board], board, board_stats_fbb);

    return true;
  }

  auto WriteTxn::create_note(FlatBufferBuilder&& builder, Offset<Note> offset) -> uint64_t {
    uint64_t id = next_id();
    set_note(id, std::move(builder), offset);
    return id;
  }
  auto WriteTxn::set_note(uint64_t id, FlatBufferBuilder&& builder, Offset<Note> offset) -> void {
    builder.Finish(offset);
    const auto note = GetRoot<Note>(builder.GetBufferPointer());
    const auto old_note_opt = get_note(id);
    auto note_stats_opt = get_note_stats(id);
    const auto page_opt = get_page(note->page());
    int64_t karma = 0;
    assert_fmt(!!page_opt, "set_note: note {:x} top-level ancestor page {:x} does not exist", id, note->page());
    const auto page = *page_opt;
    if (old_note_opt) {
      spdlog::debug("Updating comment {:x} (parent {:x}, author {:x})", id, note->parent(), note->author());
      assert(!!note_stats_opt);
      const auto old_note = *old_note_opt;
      auto note_stats = *note_stats_opt;
      karma = note_stats->karma();
      assert(note->author() == old_note->author());
      assert(note->parent() == old_note->parent());
      assert(note->page() == old_note->page());
      assert(note->created_at() == old_note->created_at());
    } else {
      spdlog::debug("Creating comment {:x} (parent {:x}, author {:x})", id, note->parent(), note->author());
      auto page_stats = get_page_stats(note->page());
      assert(!!page_stats);
      const bool is_active = note->created_at() >= page->created_at() &&
          note->created_at() - page->created_at() <= ACTIVE_COMMENT_MAX_AGE,
        is_newer = is_active && note->created_at() > (*page_stats)->newest_comment_time();
      db_put(txn, db.dbis[Owner_UserNote], Cursor(note->author(), id), note->author());
      db_put(txn, db.dbis[NotesTop_UserKarmaNote], Cursor(note->author(), karma_uint(karma), id), id);
      db_put(txn, db.dbis[NotesNew_BoardTimeNote], Cursor(page->board(), note->created_at(), id), id);
      db_put(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(page->board(), karma_uint(karma), id), id);
      db_put(txn, db.dbis[ChildrenNew_PostTimeNote], Cursor(note->parent(), note->created_at(), id), id);
      db_put(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(karma), id), id);
      {
        FlatBufferBuilder fbb;
        fbb.ForceDefaults(true);
        fbb.Finish(CreateNoteStats(fbb, note->created_at()));
        db_put(txn, db.dbis[NoteStats_Note], id, fbb);
      }
      {
        auto s = *page_stats;
        FlatBufferBuilder fbb;
        fbb.Finish(CreatePageStats(fbb,
          s->created_at(),
          is_newer ? note->created_at() : s->newest_comment_time(),
          is_active ? s->newest_comment_time_necro() : std::max(s->newest_comment_time_necro(), note->created_at()),
          s->descendant_count() + 1,
          s->upvotes(),
          s->downvotes(),
          s->karma()
        ));
        db_put(txn, db.dbis[PageStats_Page], note->page(), fbb);
      }
      if (auto parent_stats = get_note_stats(note->parent())) {
        auto s = *parent_stats;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateNoteStats(fbb,
          s->created_at(),
          s->child_count() + 1,
          s->upvotes(),
          s->downvotes(),
          s->karma()
        ));
        db_put(txn, db.dbis[NoteStats_Note], note->parent(), fbb);
      }
      if (auto user_stats = get_user_stats(note->author())) {
        auto s = *user_stats;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateUserStats(fbb,
          s->note_count() + 1,
          s->note_karma(),
          s->page_count(),
          s->page_karma()
        ));
        db_put(txn, db.dbis[UserStats_User], note->author(), fbb);
      }
      if (auto board_stats = get_board_stats(page->board())) {
        auto s = *board_stats;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateBoardStats(fbb,
          s->created_at(),
          s->page_count(),
          s->note_count() + 1,
          s->subscriber_count(),
          s->users_active_half_year(),
          s->users_active_month(),
          s->users_active_week(),
          s->users_active_day()
        ));
        db_put(txn, db.dbis[BoardStats_Board], page->board(), fbb);
      }
    }
    db_put(txn, db.dbis[Note_Note], id, builder);
  }
  auto WriteTxn::delete_note(uint64_t id) -> bool {
    const auto note_opt = get_note(id);
    if (!note_opt) {
      spdlog::warn("Tried to delete nonexistent comment {:x}", id);
      return false;
    }
    const auto note = *note_opt;
    const auto page_id = note->page();
    const auto page_opt = get_page(page_id);
    assert_fmt(!!page_opt, "delete_note: note {:x} top-level ancestor page {:x} does not exist", id, page_id);
    const auto board_id = (*page_opt)->board();

    FlatBufferBuilder page_stats_fbb, board_stats_fbb;
    optional<PageStats*> page_stats = {};
    optional<BoardStats*> board_stats = {};
    if (auto page_stats_opt = get_page_stats(page_id)) {
      auto s = *page_stats_opt;
      page_stats_fbb.Finish(CreatePageStats(page_stats_fbb,
        s->created_at(),
        s->newest_comment_time(),
        s->newest_comment_time_necro(),
        s->descendant_count(),
        s->upvotes(),
        s->downvotes(),
        s->karma()
      ));
      page_stats = { flatbuffers::GetMutableRoot<PageStats>(page_stats_fbb.GetBufferPointer()) };
    }
    if (auto board_stats_opt = get_board_stats(board_id)) {
      auto s = *board_stats_opt;
      board_stats_fbb.Finish(CreateBoardStats(board_stats_fbb,
        s->created_at(),
        s->page_count(),
        s->note_count(),
        s->subscriber_count(),
        s->users_active_half_year(),
        s->users_active_month(),
        s->users_active_week(),
        s->users_active_day()
      ));
      board_stats = { flatbuffers::GetMutableRoot<BoardStats>(board_stats_fbb.GetBufferPointer()) };
    }

    if (delete_note_for_page(id, board_id, page_stats, board_stats)) {
      if (page_stats) db_put(txn, db.dbis[PageStats_Page], page_id, page_stats_fbb);
      if (board_stats) db_put(txn, db.dbis[BoardStats_Board], board_id, board_stats_fbb);
      if (auto parent_stats = get_note_stats(note->parent())) {
        auto s = *parent_stats;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateNoteStats(fbb,
          s->created_at(),
          std::min(s->child_count(), s->child_count() - 1),
          s->upvotes(),
          s->downvotes(),
          s->karma()
        ));
        db_put(txn, db.dbis[NoteStats_Note], note->parent(), fbb);
      }
      return true;
    }
    return false;
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
    const auto page_stats_opt = get_page_stats(post_id);
    const auto note_stats_opt = get_note_stats(post_id);
    const auto op_stats_opt = get_user_stats(op_id);
    assert(!!op_stats_opt);
    if (page_stats_opt) {
      {
        const auto page = *page_opt;
        const auto s = *page_stats_opt;
        const auto old_karma = s->karma(), new_karma = old_karma + diff;
        FlatBufferBuilder fbb;
        fbb.Finish(CreatePageStats(fbb,
          s->created_at(),
          s->newest_comment_time(),
          s->newest_comment_time_necro(),
          s->descendant_count(),
          vote > 0 ? s->upvotes() + 1 : (existing > 0 ? std::min(s->upvotes(), s->upvotes() - 1) : s->upvotes()),
          vote < 0 ? s->downvotes() + 1 : (existing < 0 ? std::min(s->downvotes(), s->downvotes() - 1) : s->downvotes()),
          new_karma
        ));
        db_put(txn, db.dbis[PageStats_Page], post_id, fbb);
        db_del(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma_uint(old_karma), post_id));
        db_del(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(page->author(), karma_uint(old_karma), post_id));
        db_put(txn, db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma_uint(new_karma), post_id), post_id);
        db_put(txn, db.dbis[PagesTop_UserKarmaPage], Cursor(page->author(), karma_uint(new_karma), post_id), post_id);
      }
      {
        const auto s = *op_stats_opt;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateUserStats(fbb,
          s->note_count(),
          s->note_karma(),
          s->page_count(),
          s->page_karma() + diff
        ));
        db_put(txn, db.dbis[UserStats_User], op_id, fbb);
      }
    } else {
      {
        const auto note = *note_opt;
        const auto note_page_opt = get_page(note->page());
        assert(!!note_page_opt);
        const auto note_page = *note_page_opt;
        const auto s = *note_stats_opt;
        const auto old_karma = s->karma(), new_karma = old_karma + diff;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateNoteStats(fbb,
          s->created_at(),
          s->child_count(),
          vote > 0 ? s->upvotes() + 1 : (existing > 0 ? std::min(s->upvotes(), s->upvotes() - 1) : s->upvotes()),
          vote < 0 ? s->downvotes() + 1 : (existing < 0 ? std::min(s->downvotes(), s->downvotes() - 1) : s->downvotes()),
          new_karma
        ));
        db_put(txn, db.dbis[NoteStats_Note], post_id, fbb);
        db_del(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(note_page->board(), karma_uint(old_karma), post_id));
        db_del(txn, db.dbis[NotesTop_UserKarmaNote], Cursor(note->author(), karma_uint(old_karma), post_id));
        db_del(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(old_karma), post_id));
        db_put(txn, db.dbis[NotesTop_BoardKarmaNote], Cursor(note_page->board(), karma_uint(new_karma), post_id), post_id);
        db_put(txn, db.dbis[NotesTop_UserKarmaNote], Cursor(note->author(), karma_uint(new_karma), post_id), post_id);
        db_put(txn, db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(new_karma), post_id), post_id);
      }
      {
        const auto s = *op_stats_opt;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateUserStats(fbb,
          s->note_count(),
          s->note_karma() + diff,
          s->page_count(),
          s->page_karma()
        ));
        db_put(txn, db.dbis[UserStats_User], op_id, fbb);
      }
    }
  }
}
