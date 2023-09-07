#include <random>
#include <vector>
#include <spdlog/spdlog.h>
#include <duthomhas/csprng.hpp>
#include "db.h++"

using std::optional, flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot, flatbuffers::Offset;

#define assert_fmt(CONDITION, ...) if (!(CONDITION)) { spdlog::critical(__VA_ARGS__); throw std::runtime_error("Assertion failed: " #CONDITION); }

namespace Ludwig {
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

  DB::DB(const char* filename) : env(filename, MDB_NOSUBDIR | MDB_WRITEMAP, 0600) {
#   define MK_DBI(NAME, FLAGS) dbis[NAME] = env.openDB(#NAME, FLAGS | MDB_CREATE);
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
    MDBOutVal val;
    auto txn = env.getRWTransaction();
    if (txn->get(dbis[Settings], SettingsKey::hash_seed, val)) {
      spdlog::info("Opened database {} for the first time, generating secrets", filename);
      duthomhas::csprng rng;
      rng(seed);
      rng(jwt_secret);
      const auto now = now_s();
      txn->put(dbis[Settings], SettingsKey::next_id, { 1ULL });
      txn->put(dbis[Settings], SettingsKey::hash_seed, { seed });
      txn->put(dbis[Settings], SettingsKey::jwt_secret, MDBInVal({ .d_mdbval = { JWT_SECRET_SIZE, jwt_secret } }));
      txn->put(dbis[Settings], SettingsKey::domain, { "http://localhost:2023" });
      txn->put(dbis[Settings], SettingsKey::created_at, { now });
      txn->put(dbis[Settings], SettingsKey::updated_at, { now });
      txn->put(dbis[Settings], SettingsKey::name, { "Ludwig" });
      txn->put(dbis[Settings], SettingsKey::description, { "A new Ludwig server" });
      txn->put(dbis[Settings], SettingsKey::post_max_length, { 1024 * 1024 });
      txn->put(dbis[Settings], SettingsKey::media_upload_enabled, { 0ULL });
      txn->put(dbis[Settings], SettingsKey::board_creation_admin_only, { 1ULL });
      txn->put(dbis[Settings], SettingsKey::federation_enabled, { 0ULL });
    } else {
      spdlog::debug("Loaded existing database {}", filename);
      seed = val.get<uint64_t>();
      auto err = txn->get(dbis[Settings], SettingsKey::jwt_secret, val);
      assert_fmt(!err, "Failed to load JWT secret from database {}: {}", filename, mdb_strerror(err));
      assert_fmt(val.d_mdbval.mv_size == JWT_SECRET_SIZE, "jwt_secret is wrong size: expected {}, got {}", JWT_SECRET_SIZE, val.d_mdbval.mv_size);
      memcpy(jwt_secret, val.d_mdbval.mv_data, JWT_SECRET_SIZE);
    }
    txn->commit();
  }

  static auto int_key(MDBOutVal& k, MDBOutVal&) -> uint64_t {
    return k.get<uint64_t>();
  };
  static auto second_key(MDBOutVal& k, MDBOutVal&) -> uint64_t {
    return Cursor(k).int_field_1();
  };
  static auto third_key(MDBOutVal& k, MDBOutVal&) -> uint64_t {
    return Cursor(k).int_field_2();
  };

  static inline auto count(MDBDbi dbi, MDBROTransactionImpl& txn, optional<Cursor> from = {}, optional<Cursor> to = {}) -> uint64_t {
    DBIter<void> iter(dbi, txn, from, to, [](MDBOutVal&, MDBOutVal&){});
    uint64_t n = 0;
    while (!iter.is_done()) {
      ++n;
      ++iter;
    }
    return n;
  }

  auto ReadTxnBase::get_setting_str(MDBInVal key) -> std::string_view {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Settings], key, v)) return {};
    return v.get<std::string_view>();
  }
  auto ReadTxnBase::get_setting_int(MDBInVal key) -> uint64_t {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Settings], key, v)) return {};
    return v.get<uint64_t>();
  }

  auto ReadTxnBase::get_user_id(std::string_view name) -> optional<uint64_t> {
    // TODO: Handle hash collisions, maybe double hashing.
    MDBOutVal v;
    if (ro_txn().get(db.dbis[User_Name], Cursor(name, db.seed).in_val(), v)) return {};
    return { v.get<uint64_t>() };
  }
  auto ReadTxnBase::get_user(uint64_t id) -> optional<const User*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[User_User], { id }, v)) return {};
    return { GetRoot<User>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::get_user_stats(uint64_t id) -> optional<const UserStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[UserStats_User], { id }, v)) return {};
    return { GetRoot<UserStats>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::get_local_user(uint64_t id) -> optional<const LocalUser*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[LocalUser_User], { id }, v)) return {};
    return { GetRoot<LocalUser>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::count_local_users() -> uint64_t {
    return count(db.dbis[LocalUser_User], ro_txn());
  }
  auto ReadTxnBase::list_users(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[User_User], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxnBase::list_local_users(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[LocalUser_User], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxnBase::list_subscribers(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Subscription_BoardUser],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, 0)),
      { Cursor(board_id, ID_MAX) },
      second_key
    );
  }
  auto ReadTxnBase::user_is_subscribed(uint64_t user_id, uint64_t board_id) -> bool {
    MDBOutVal v;
    return !ro_txn().get(db.dbis[Subscription_UserBoard], Cursor(user_id, board_id).in_val(), v);
  }

  auto ReadTxnBase::get_board_id(std::string_view name) -> optional<uint64_t> {
    // TODO: Handle hash collisions, maybe double hashing.
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Board_Name], Cursor(name, db.seed).in_val(), v)) return {};
    return { v.get<uint64_t>() };
  }
  auto ReadTxnBase::get_board(uint64_t id) -> optional<const Board*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Board_Board], { id }, v)) return {};
    return { GetRoot<Board>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::get_board_stats(uint64_t id) -> optional<const BoardStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[BoardStats_Board], { id }, v)) return {};
    return { GetRoot<BoardStats>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::get_local_board(uint64_t id) -> optional<const LocalBoard*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[LocalBoard_Board], { id }, v)) return {};
    return { GetRoot<LocalBoard>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::count_local_boards() -> uint64_t {
    return count(db.dbis[LocalBoard_Board], ro_txn());
  }
  auto ReadTxnBase::list_boards(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[Board_Board], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxnBase::list_local_boards(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[LocalBoard_Board], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxnBase::list_subscribed_boards(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Subscription_UserBoard],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, ID_MAX) },
      second_key
    );
  }
  auto ReadTxnBase::list_created_boards(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Owner_UserBoard],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, ID_MAX) },
      second_key
    );
  }

  auto ReadTxnBase::get_page(uint64_t id) -> optional<const Page*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Page_Page], { id }, v)) return {};
    return { GetRoot<Page>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::get_page_stats(uint64_t id) -> optional<const PageStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[PageStats_Page], { id }, v)) return {};
    return { GetRoot<PageStats>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::list_pages_of_board_new(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[PagesNew_BoardTimePage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_pages_of_board_top(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[PagesTop_BoardKarmaPage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_pages_of_user_new(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[Owner_UserPage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, ID_MAX)),
      { Cursor(user_id, 0) },
      second_key
    );
  }
  auto ReadTxnBase::list_pages_of_user_top(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[PagesTop_UserKarmaPage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, ID_MAX, ID_MAX)),
      { Cursor(user_id, 0, 0) }
    );
  }

  auto ReadTxnBase::get_note(uint64_t id) -> optional<const Note*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Note_Note], { id }, v)) return {};
    return { GetRoot<Note>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::get_note_stats(uint64_t id) -> optional<const NoteStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[NoteStats_Note], { id }, v)) return {};
    return { GetRoot<NoteStats>(v.d_mdbval.mv_data) };
  }
  auto ReadTxnBase::list_notes_of_post_new(uint64_t post_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ChildrenNew_PostTimeNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(post_id, ID_MAX, ID_MAX)),
      { Cursor(post_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_post_top(uint64_t post_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[ChildrenTop_PostKarmaNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(post_id, ID_MAX, ID_MAX)),
      { Cursor(post_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_board_new(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[NotesNew_BoardTimeNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_board_top(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[NotesTop_BoardKarmaNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, ID_MAX, ID_MAX)),
      { Cursor(board_id, 0, 0) }
    );
  }
  auto ReadTxnBase::list_notes_of_user_new(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[Owner_UserNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, ID_MAX)),
      { Cursor(user_id, 0) },
      second_key
    );
  }
  auto ReadTxnBase::list_notes_of_user_top(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIterReverse<uint64_t>(
      db.dbis[NotesTop_UserKarmaNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, ID_MAX, ID_MAX)),
      { Cursor(user_id, 0, 0) }
    );
  }

  auto ReadTxnBase::get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Vote_UserPost], Cursor(user_id, post_id).in_val(), v)) return NoVote;
    return (Vote)v.get<int8_t>();
  }

  static inline auto delete_range(
    MDBRWTransactionImpl* txn,
    MDBDbi dbi,
    Cursor from,
    Cursor to,
    const std::function<void(MDBOutVal& k, MDBOutVal& v)>& fn = [](MDBOutVal&, MDBOutVal&){}
  ) -> void {
    auto cur = txn->getRWCursor(dbi);
    MDBOutVal k = from.out_val(), v;
    auto err = cur.get(k, v, MDB_SET_RANGE);
    auto end = to.val();
    while (!err && mdb_cmp(*txn, dbi, &k.d_mdbval, &end) < 0) {
      fn(k, v);
      err = cur.del() || cur.next(k, v);
    }
  }

# define INCREMENT_FIELD_OPT(OPT, FIELD, N) (*OPT)->mutate_##FIELD(std::max((*OPT)->FIELD(),(*OPT)->FIELD()+(N)))
# define DECREMENT_FIELD_OPT(OPT, FIELD, N) (*OPT)->mutate_##FIELD(std::min((*OPT)->FIELD(),(*OPT)->FIELD()-(N)))

  auto WriteTxn::get_user_stats_rw(uint64_t id) -> optional<UserStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[UserStats_User], { id }, v)) return {};
    return { flatbuffers::GetMutableRoot<UserStats>(v.d_mdbval.mv_data) };
  }
  auto WriteTxn::get_board_stats_rw(uint64_t id) -> optional<BoardStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[BoardStats_Board], { id }, v)) return {};
    return { flatbuffers::GetMutableRoot<BoardStats>(v.d_mdbval.mv_data) };
  }
  auto WriteTxn::get_page_stats_rw(uint64_t id) -> optional<PageStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[PageStats_Page], { id }, v)) return {};
    return { flatbuffers::GetMutableRoot<PageStats>(v.d_mdbval.mv_data) };
  }
  auto WriteTxn::get_note_stats_rw(uint64_t id) -> optional<NoteStats*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[NoteStats_Note], { id }, v)) return {};
    return { flatbuffers::GetMutableRoot<NoteStats>(v.d_mdbval.mv_data) };
  }

  auto WriteTxn::next_id() -> uint64_t {
    MDBOutVal v;
    txn->get(db.dbis[Settings], SettingsKey::next_id, v);
    return (*reinterpret_cast<uint64_t*>(v.d_mdbval.mv_data))++;
  }
  auto WriteTxn::create_user(FlatBufferBuilder&& builder, Offset<User> offset) -> uint64_t {
    uint64_t id = next_id();
    set_user(id, std::move(builder), offset);
    return id;
  }
  auto WriteTxn::set_user(uint64_t id, FlatBufferBuilder&& builder, Offset<User> offset) -> void {
    builder.Finish(offset);
    const MDBInVal id_val(id), v({ .d_mdbval = { builder.GetSize(), builder.GetBufferPointer() } });
    auto user = GetRoot<User>(builder.GetBufferPointer());
    auto old_user_opt = get_user(id);
    if (old_user_opt) {
      spdlog::debug("Updating user {:x} (name {})", id, user->name()->string_view());
      auto old_user = *old_user_opt;
      if (user->name() != old_user->name()) {
        txn->del(db.dbis[User_Name], Cursor(old_user->name()->string_view(), db.seed).in_val());
      }
    } else {
      spdlog::debug("Creating user {:x} (name {})", id, user->name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateUserStats(fbb));
      const MDBInVal stats_val({ .d_mdbval = { fbb.GetSize(), fbb.GetBufferPointer() } });
      txn->put(db.dbis[UserStats_User], id_val, stats_val);
    }
    txn->put(db.dbis[User_User], id_val, v);
    txn->put(db.dbis[User_Name], Cursor(user->name()->string_view(), db.seed).in_val(), id_val);

    // TODO: Handle media (avatar)
  }
  auto WriteTxn::set_local_user(uint64_t id, FlatBufferBuilder&& builder, Offset<LocalUser> offset) -> void {
    builder.Finish(offset);
    const MDBInVal id_val(id), v({ .d_mdbval = { builder.GetSize(), builder.GetBufferPointer() } });
    txn->put(db.dbis[LocalUser_User], id_val, v);
  }
  auto WriteTxn::delete_user(uint64_t id) -> bool {
    auto user_opt = get_user(id);
    if (!user_opt) {
      spdlog::warn("Tried to delete nonexistent user {:x}", id);
      return false;
    }

    spdlog::debug("Deleting user {:x}", id);
    const MDBInVal id_val(id);
    txn->del(db.dbis[User_User], id_val);
    txn->del(db.dbis[User_Name], Cursor((*user_opt)->name()->string_view(), db.seed).in_val());
    txn->del(db.dbis[UserStats_User], id_val);
    txn->del(db.dbis[LocalUser_User], id_val);

    delete_range(txn.get(), db.dbis[Subscription_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        auto board = c.int_field_1();
        auto board_stats = get_board_stats_rw(board);
        txn->del(db.dbis[Subscription_BoardUser], Cursor(board, c.int_field_0()).in_val());
        if (board_stats) DECREMENT_FIELD_OPT(board_stats, subscriber_count, 1);
      }
    );
    delete_range(txn.get(), db.dbis[Owner_UserPage], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn.get(), db.dbis[Owner_UserNote], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn.get(), db.dbis[Owner_UserBoard], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn.get(), db.dbis[Owner_UserMedia], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn.get(), db.dbis[Owner_UserUrl], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn.get(), db.dbis[PagesTop_UserKarmaPage], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn.get(), db.dbis[NotesTop_UserKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn.get(), db.dbis[Vote_UserPost], Cursor(id, 0), Cursor(id, ID_MAX),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Vote_PostUser], Cursor(c.int_field_1(), c.int_field_0()).in_val());
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
    const MDBInVal id_val(id), v({ .d_mdbval = { builder.GetSize(), builder.GetBufferPointer() } });
    auto board = GetRoot<Board>(builder.GetBufferPointer());
    auto old_board_opt = get_board(id);
    if (old_board_opt) {
      spdlog::debug("Updating board {:x} (name {})", id, board->name()->string_view());
      auto old_board = *old_board_opt;
      if (board->name() != old_board->name()) {
        txn->del(db.dbis[Board_Name], Cursor(old_board->name()->string_view(), db.seed).in_val());
      }
    } else {
      spdlog::debug("Creating board {:x} (name {})", id, board->name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateBoardStats(fbb, board->created_at()));
      const MDBInVal stats_val({ .d_mdbval = { fbb.GetSize(), fbb.GetBufferPointer() } });
      txn->put(db.dbis[BoardStats_Board], id_val, stats_val);
    }
    txn->put(db.dbis[Board_Board], id_val, v);
    txn->put(db.dbis[Board_Name], Cursor(board->name()->string_view(), db.seed).in_val(), id_val);

    // TODO: Handle media (avatar, banner)
  }
  auto WriteTxn::set_local_board(uint64_t id, FlatBufferBuilder&& builder, Offset<LocalBoard> offset) -> void {
    builder.Finish(offset);
    const MDBInVal id_val(id), v({ .d_mdbval = { builder.GetSize(), builder.GetBufferPointer() } });
    auto board = GetRoot<LocalBoard>(builder.GetBufferPointer());
    assert_fmt(!!get_user(board->owner()), "set_local_board: board {:x} owner user {:x} does not exist", id, board->owner());
    auto old_board_opt = get_local_board(id);
    if (old_board_opt) {
      spdlog::debug("Updating local board {:x}", id);
      auto old_board = *old_board_opt;
      if (board->owner() != old_board->owner()) {
        spdlog::info("Changing owner of local board {:x}: {:x} -> {:x}", id, old_board->owner(), board->owner());
        txn->del(db.dbis[Owner_UserBoard], Cursor(old_board->owner(), id).in_val());
      }
    } else {
      spdlog::debug("Updating local board {:x}", id);
    }
    txn->put(db.dbis[Owner_UserBoard], Cursor(board->owner(), id).in_val(), { board->owner() });
    txn->put(db.dbis[LocalBoard_Board], id_val, v);
  }
  auto WriteTxn::delete_board(uint64_t id) -> bool {
    auto board_opt = get_board(id);
    if (!board_opt) {
      spdlog::warn("Tried to delete nonexistent board {:x}", id);
      return false;
    }

    spdlog::debug("Deleting board {:x}", id);
    const MDBInVal id_val(id);
    txn->del(db.dbis[Board_Board], id_val);
    txn->del(db.dbis[Board_Name], Cursor((*board_opt)->name()->string_view(), db.seed).in_val());
    txn->del(db.dbis[BoardStats_Board], id_val);
    txn->del(db.dbis[LocalBoard_Board], id_val);
    // TODO: Owner_UserBoard

    delete_range(txn.get(), db.dbis[Subscription_BoardUser], Cursor(id, 0), Cursor(id, ID_MAX),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Subscription_UserBoard], Cursor(c.int_field_1(), c.int_field_0()).in_val());
      }
    );
    delete_range(txn.get(), db.dbis[PagesNew_BoardTimePage], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn.get(), db.dbis[PagesTop_BoardKarmaPage], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn.get(), db.dbis[NotesNew_BoardTimeNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));
    delete_range(txn.get(), db.dbis[NotesTop_BoardKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    return true;
  }
  auto WriteTxn::set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
    MDBOutVal v;
    bool existing = !txn->get(db.dbis[Subscription_BoardUser], Cursor(board_id, user_id).in_val(), v);
    auto board_stats = get_board_stats_rw(board_id);
    if (subscribed) {
      assert_fmt(!!get_user(user_id), "set_subscription: user {:x} does not exist", user_id);
      assert_fmt(!!board_stats, "set_subscription: board {:x} does not exist", board_id);
      if (!existing) {
        spdlog::debug("Subscribing user {:x} to board {:x}", user_id, board_id);
        auto now = now_s();
        txn->put(db.dbis[Subscription_BoardUser], Cursor(board_id, user_id).in_val(), { now });
        txn->put(db.dbis[Subscription_UserBoard], Cursor(user_id, board_id).in_val(), { now });
        INCREMENT_FIELD_OPT(board_stats, subscriber_count, 1);
      }
    } else if (existing) {
      spdlog::debug("Unsubscribing user {:x} from board {:x}", user_id, board_id);
      txn->del(db.dbis[Subscription_BoardUser], Cursor(board_id, user_id).in_val());
      txn->del(db.dbis[Subscription_UserBoard], Cursor(user_id, board_id).in_val());
      if (board_stats) DECREMENT_FIELD_OPT(board_stats, subscriber_count, 1);
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
    const MDBInVal id_val(id), v({ .d_mdbval = { builder.GetSize(), builder.GetBufferPointer() } });
    auto page = GetRoot<Page>(builder.GetBufferPointer());
    auto old_page_opt = get_page(id);
    auto user_stats = get_user_stats_rw(page->author());
    auto board_stats = get_board_stats_rw(page->board());
    assert_fmt(!!user_stats, "set_page: post {:x} author user {:x} does not exist", id, page->author());
    assert_fmt(!!board_stats, "set_page: post {:x} board {:x} does not exist", id, page->board());
    int64_t karma = 0;
    if (old_page_opt) {
      spdlog::debug("Updating top-level post {:x} (board {:x}, author {:x})", id, page->board(), page->author());
      auto page_stats_opt = get_page_stats_rw(id);
      assert_fmt(!!page_stats_opt, "set_page: page stats not in database for existing page {:x}", id);
      karma = (*page_stats_opt)->karma();
      auto old_page = *old_page_opt;
      assert_fmt(page->author() == old_page->author(), "set_page: cannot change author of page {:x}", id);
      assert_fmt(page->created_at() == old_page->created_at(), "set_page: cannot change created_at of page {:x}", id);
      if (page->board() != old_page->board()) {
        txn->del(db.dbis[PagesNew_BoardTimePage], Cursor(old_page->board(), page->created_at(), id).in_val());
        txn->del(db.dbis[PagesTop_BoardKarmaPage], Cursor(old_page->board(), karma_uint(karma), id).in_val());
        auto board_stats = get_board_stats_rw(old_page->board());
        if (board_stats) DECREMENT_FIELD_OPT(board_stats, page_count, 1);
      }
    } else {
      spdlog::debug("Creating top-level post {:x} (board {:x}, author {:x})", id, page->board(), page->author());
      txn->put(db.dbis[Owner_UserPage], Cursor(page->author(), id).in_val(), { page->author() });
      txn->put(db.dbis[PagesTop_UserKarmaPage], Cursor(page->author(), 1, id).in_val(), id_val);
    txn->put(db.dbis[PagesNew_BoardTimePage], Cursor(page->board(), page->created_at(), id).in_val(), id_val);
    txn->put(db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma_uint(karma), id).in_val(), id_val);
      {
        FlatBufferBuilder fbb;
        fbb.ForceDefaults(true);
        fbb.Finish(CreatePageStats(fbb, page->created_at()));
        const MDBInVal stats_val({ .d_mdbval = { fbb.GetSize(), fbb.GetBufferPointer() } });
        txn->put(db.dbis[PageStats_Page], id_val, stats_val);
      }
      INCREMENT_FIELD_OPT(user_stats, page_count, 1);
      INCREMENT_FIELD_OPT(board_stats, page_count, 1);
    }
    txn->put(db.dbis[Page_Page], id_val, v);
  }
  auto WriteTxn::delete_note_for_page(uint64_t id, uint64_t board_id, std::optional<PageStats*> page_stats) -> bool {
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
    {
      auto parent_stats = get_note_stats_rw(parent);
      auto user_stats = get_user_stats_rw(author);
      if (page_stats) DECREMENT_FIELD_OPT(page_stats, descendant_count, 1);
      if (parent_stats) DECREMENT_FIELD_OPT(parent_stats, child_count, 1);
      if (user_stats) {
        if (karma > 0) DECREMENT_FIELD_OPT(user_stats, note_karma, karma);
        else if (karma < 0) INCREMENT_FIELD_OPT(user_stats, note_karma, -karma);
        DECREMENT_FIELD_OPT(user_stats, note_count, 1);
      }

      auto board_stats = get_board_stats_rw(board_id);
      txn->del(db.dbis[NotesNew_BoardTimeNote], Cursor(board_id, created_at, id).in_val());
      txn->del(db.dbis[NotesTop_BoardKarmaNote], Cursor(board_id, karma_uint(karma), id).in_val());
      if (board_stats) DECREMENT_FIELD_OPT(board_stats, note_count, 1);
    }

    const MDBInVal id_val(id);
    delete_range(txn.get(), db.dbis[Vote_PostUser], Cursor(id, 0), Cursor(id, ID_MAX),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Vote_UserPost], Cursor(c.int_field_1(), c.int_field_0()).in_val());
      }
    );
    std::set<uint64_t> children;
    delete_range(txn.get(), db.dbis[ChildrenNew_PostTimeNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX),
      [&children](MDBOutVal&, MDBOutVal& v) {
        children.insert(v.get<uint64_t>());
      }
    );
    delete_range(txn.get(), db.dbis[ChildrenTop_PostKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    txn->del(db.dbis[Note_Note], id_val);
    txn->del(db.dbis[NoteStats_Note], id_val);
    txn->del(db.dbis[Owner_UserNote], Cursor(author, id).in_val());
    txn->del(db.dbis[ChildrenNew_PostTimeNote], Cursor(parent, created_at, id).in_val());
    txn->del(db.dbis[ChildrenTop_PostKarmaNote], Cursor(parent, karma_uint(karma), id).in_val());

    for (uint64_t child : children) {
      assert(child != id);
      delete_note_for_page(child, board_id, page_stats);
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
    {
      const auto user_stats = get_user_stats_rw(author);
      const auto board_stats = get_board_stats_rw(board);
      if (user_stats) {
        if (karma > 0) DECREMENT_FIELD_OPT(user_stats, page_karma, karma);
        else if (karma < 0) INCREMENT_FIELD_OPT(user_stats, page_karma, -karma);
        DECREMENT_FIELD_OPT(user_stats, page_count, 1);
      }
      if (board_stats) DECREMENT_FIELD_OPT(board_stats, page_count, 1);
    }

    const MDBInVal id_val(id);

    delete_range(txn.get(), db.dbis[Vote_PostUser], Cursor(id, 0), Cursor(id, ID_MAX),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Vote_UserPost], Cursor(c.int_field_1(), c.int_field_0()).in_val());
      }
    );
    std::set<uint64_t> children;
    delete_range(txn.get(), db.dbis[ChildrenNew_PostTimeNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX),
      [&children](MDBOutVal&, MDBOutVal& v) {
        children.insert(v.get<uint64_t>());
      }
    );
    delete_range(txn.get(), db.dbis[ChildrenTop_PostKarmaNote], Cursor(id, 0, 0), Cursor(id, ID_MAX, ID_MAX));

    txn->del(db.dbis[Page_Page], id_val);
    txn->del(db.dbis[PageStats_Page], id_val);
    txn->del(db.dbis[Owner_UserPage], Cursor(author, id).in_val());
    txn->del(db.dbis[PagesTop_UserKarmaPage], Cursor(author, karma_uint(karma), id).in_val());
    txn->del(db.dbis[PagesNew_BoardTimePage], Cursor(board, created_at, id).in_val());
    txn->del(db.dbis[PagesTop_BoardKarmaPage], Cursor(board, karma_uint(karma), id).in_val());

    for (uint64_t child : children) delete_note_for_page(child, board, {});

    return true;
  }

  auto WriteTxn::create_note(FlatBufferBuilder&& builder, Offset<Note> offset) -> uint64_t {
    uint64_t id = next_id();
    set_note(id, std::move(builder), offset);
    return id;
  }
  auto WriteTxn::set_note(uint64_t id, FlatBufferBuilder&& builder, Offset<Note> offset) -> void {
    builder.Finish(offset);
    const MDBInVal id_val(id), v({ .d_mdbval = { builder.GetSize(), builder.GetBufferPointer() } });
    const auto note = GetRoot<Note>(builder.GetBufferPointer());
    const auto old_note_opt = get_note(id);
    auto note_stats_opt = get_note_stats_rw(id);
    const auto page_opt = get_page(note->page());
    int64_t karma = 0;
    assert_fmt(!!page_opt, "set_note: note {:x} top-level ancestor page {:x} does not exist", id, note->page());
    const auto page = *page_opt;
    auto page_stats = get_page_stats_rw(note->page());
    auto parent_stats = get_note_stats_rw(note->parent());
    auto board_stats = get_board_stats_rw(page->board());
    auto user_stats = get_user_stats_rw(note->author());
    assert_fmt(!!page_stats, "set_note: note {:x} top-level ancestor page {:x} does not have page_stats", id, note->page());
    assert_fmt(!!board_stats, "set_note: note {:x} board {:x} does not exist", id, page->board());
    assert_fmt(!!user_stats, "set_note: note {:x} author user {:x} does not exist", id, note->author());
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
      txn->put(db.dbis[Owner_UserNote], Cursor(note->author(), id).in_val(), { note->author() });
      txn->put(db.dbis[NotesTop_UserKarmaNote], Cursor(note->author(), karma_uint(karma), id).in_val(), id_val);
      txn->put(db.dbis[NotesNew_BoardTimeNote], Cursor(page->board(), note->created_at(), id).in_val(), id_val);
      txn->put(db.dbis[NotesTop_BoardKarmaNote], Cursor(page->board(), karma_uint(karma), id).in_val(), id_val);
      txn->put(db.dbis[ChildrenNew_PostTimeNote], Cursor(note->parent(), note->created_at(), id).in_val(), id_val);
      txn->put(db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(karma), id).in_val(), id_val);
      {
        FlatBufferBuilder fbb;
        fbb.ForceDefaults(true);
        fbb.Finish(CreateNoteStats(fbb, note->created_at()));
        const MDBInVal stats_val({ .d_mdbval = { fbb.GetSize(), fbb.GetBufferPointer() } });
        txn->put(db.dbis[NoteStats_Note], id_val, stats_val);
      }
      INCREMENT_FIELD_OPT(page_stats, descendant_count, 1);
      INCREMENT_FIELD_OPT(user_stats, note_count, 1);
      INCREMENT_FIELD_OPT(board_stats, note_count, 1);
      if (parent_stats) INCREMENT_FIELD_OPT(parent_stats, child_count, 1);
    }
    txn->put(db.dbis[Note_Note], id_val, v);
  }
  auto WriteTxn::delete_note(uint64_t id) -> bool {
    const auto note_opt = get_note(id);
    if (!note_opt) {
      spdlog::warn("Tried to delete nonexistent comment {:x}", id);
      return false;
    }
    const auto page_id = (*note_opt)->page();
    const auto page = get_page(page_id);
    assert_fmt(!!page, "delete_note: note {:x} top-level ancestor page {:x} does not exist", id, page_id);
    return delete_note_for_page(id, (*page)->board(), get_page_stats_rw(page_id));
  }

  auto WriteTxn::set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    const auto existing = get_vote_of_user_for_post(user_id, post_id);
    const int64_t diff = vote - existing;
    if (!diff) return;
    const auto page = get_page(post_id);
    const auto note = page ? std::nullopt : get_note(post_id);
    assert(page || note);
    auto op_id = page ? (*page)->author() : (*note)->author();
    const auto op = get_user(op_id);
    assert(!!op);
    spdlog::debug("Setting vote from user {:x} on post {:x} to {}", user_id, post_id, (int8_t)vote);
    if (vote) {
      txn->put(db.dbis[Vote_UserPost], Cursor(user_id, post_id).in_val(), { (int8_t)vote });
      txn->put(db.dbis[Vote_PostUser], Cursor(post_id, user_id).in_val(), { (int8_t)vote });
    } else {
      txn->del(db.dbis[Vote_UserPost], Cursor(user_id, post_id).in_val());
      txn->del(db.dbis[Vote_PostUser], Cursor(post_id, user_id).in_val());
    }
    auto page_stats = get_page_stats_rw(post_id);
    auto note_stats = get_note_stats_rw(post_id);
    auto op_stats = get_user_stats_rw(op_id);
    if (page_stats) {
      auto old_karma = (*page_stats)->karma(), new_karma = old_karma + diff;
      (*page_stats)->mutate_karma(new_karma);
      (*op_stats)->mutate_page_karma(new_karma);
      if (existing < 0) DECREMENT_FIELD_OPT(page_stats, downvotes, 1);
      else if (existing > 0) DECREMENT_FIELD_OPT(page_stats, upvotes, 1);
      if (vote > 0) INCREMENT_FIELD_OPT(page_stats, upvotes, 1);
      else if (vote < 0) INCREMENT_FIELD_OPT(page_stats, downvotes, 1);
      txn->del(db.dbis[PagesTop_BoardKarmaPage], Cursor((*page)->board(), karma_uint(old_karma), post_id).in_val());
      txn->del(db.dbis[PagesTop_UserKarmaPage], Cursor((*page)->author(), karma_uint(old_karma), post_id).in_val());
      txn->put(db.dbis[PagesTop_BoardKarmaPage], Cursor((*page)->board(), karma_uint(new_karma), post_id).in_val(), post_id);
      txn->put(db.dbis[PagesTop_UserKarmaPage], Cursor((*page)->author(), karma_uint(new_karma), post_id).in_val(), post_id);
    } else {
      auto old_karma = (*note_stats)->karma(), new_karma = old_karma + diff;
      (*note_stats)->mutate_karma(new_karma);
      (*op_stats)->mutate_note_karma(new_karma);
      if (existing < 0) DECREMENT_FIELD_OPT(note_stats, downvotes, 1);
      else if (existing > 0) DECREMENT_FIELD_OPT(note_stats, upvotes, 1);
      if (vote > 0) INCREMENT_FIELD_OPT(note_stats, upvotes, 1);
      else if (vote < 0) INCREMENT_FIELD_OPT(note_stats, downvotes, 1);
      auto note_page = get_page((*note)->page());
      assert(!!note_page);
      txn->del(db.dbis[NotesTop_BoardKarmaNote], Cursor((*note_page)->board(), karma_uint(old_karma), post_id).in_val());
      txn->del(db.dbis[NotesTop_UserKarmaNote], Cursor((*note)->author(), karma_uint(old_karma), post_id).in_val());
      txn->del(db.dbis[ChildrenTop_PostKarmaNote], Cursor((*note)->parent(), karma_uint(old_karma), post_id).in_val());
      txn->put(db.dbis[NotesTop_BoardKarmaNote], Cursor((*note_page)->board(), karma_uint(new_karma), post_id).in_val(), post_id);
      txn->put(db.dbis[NotesTop_UserKarmaNote], Cursor((*note)->author(), karma_uint(new_karma), post_id).in_val(), post_id);
      txn->put(db.dbis[ChildrenTop_PostKarmaNote], Cursor((*note)->parent(), karma_uint(new_karma), post_id).in_val(), post_id);
    }
  }
}
