#include <random>
#include <spdlog/spdlog.h>
#include "db.h++"
#include "id.h++"

using std::optional, flatbuffers::FlatBufferBuilder, flatbuffers::GetRoot, flatbuffers::Offset;

namespace Ludwig {
  enum Dbi {
    Settings,

    User_User,
    User_Name,
    LocalUser_User,
    Activity_User,
    Owner_UserBoard,
    Owner_UserPage,
    Owner_UserNote,
    PagesTop_UserKarmaPage,
    NotesTop_UserKarmaNote,
    Bookmark_UserPost,
    Subscription_UserBoard,
    PageCount_User,
    NoteCount_User,
    Karma_User,
    Owner_UserMedia,
    Owner_UserUrl,

    Board_Board,
    Board_Name,
    LocalBoard_Board,
    Activity_Board,
    PagesTop_BoardKarmaPage,
    PagesNew_BoardTimePage,
    NotesTop_BoardKarmaNote,
    NotesNew_BoardTimeNote,
    PageCount_Board,
    NoteCount_Board,
    Subscription_BoardUser,
    SubscriberCount_Board,

    Page_Page,
    Note_Note,
    Activity_Post,
    ChildrenNew_PostTimeNote,
    ChildrenTop_PostKarmaNote,
    Descendant_PostNote,
    Karma_Post,
    ChildCount_Post,
    DescendantCount_Post,
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

  DB::DB(const char* filename) : env(filename, MDB_NOSUBDIR, 0600) {
#   define MK_DBI(NAME, FLAGS) dbis[NAME] = env.openDB(#NAME, FLAGS | MDB_CREATE);
    MK_DBI(Settings, 0)

    MK_DBI(User_User, MDB_INTEGERKEY)
    MK_DBI(User_Name, MDB_INTEGERKEY)
    MK_DBI(LocalUser_User, MDB_INTEGERKEY)
    MK_DBI(Activity_User, MDB_INTEGERKEY)
    MK_DBI(Owner_UserBoard, 0)
    MK_DBI(Owner_UserPage, 0)
    MK_DBI(Owner_UserNote, 0)
    MK_DBI(Bookmark_UserPost, 0)
    MK_DBI(Subscription_UserBoard, 0)
    MK_DBI(PageCount_User, MDB_INTEGERKEY)
    MK_DBI(NoteCount_User, MDB_INTEGERKEY)
    MK_DBI(Karma_User, MDB_INTEGERKEY)
    MK_DBI(Owner_UserMedia, 0)
    MK_DBI(Owner_UserUrl, 0)

    MK_DBI(Board_Board, MDB_INTEGERKEY)
    MK_DBI(Board_Name, MDB_INTEGERKEY)
    MK_DBI(LocalBoard_Board, MDB_INTEGERKEY)
    MK_DBI(Activity_Board, MDB_INTEGERKEY)
    MK_DBI(PagesTop_BoardKarmaPage, 0)
    MK_DBI(PagesNew_BoardTimePage, 0)
    MK_DBI(NotesTop_BoardKarmaNote, 0)
    MK_DBI(NotesNew_BoardTimeNote, 0)
    MK_DBI(Descendant_PostNote, 0)
    MK_DBI(PageCount_Board, MDB_INTEGERKEY)
    MK_DBI(NoteCount_Board, MDB_INTEGERKEY)
    MK_DBI(Subscription_BoardUser, 0)
    MK_DBI(SubscriberCount_Board, MDB_INTEGERKEY)

    MK_DBI(Page_Page, MDB_INTEGERKEY)
    MK_DBI(Note_Note, MDB_INTEGERKEY)
    MK_DBI(Activity_Post, MDB_INTEGERKEY)
    MK_DBI(ChildrenNew_PostTimeNote, 0)
    MK_DBI(ChildrenTop_PostKarmaNote, 0)
    MK_DBI(Karma_Post, MDB_INTEGERKEY)
    MK_DBI(ChildCount_Post, MDB_INTEGERKEY)
    MK_DBI(DescendantCount_Post, MDB_INTEGERKEY)
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

    // Load the hash seed, or generate one if missing
    MDBOutVal seed_val;
    auto txn = env.getRWTransaction();
    if (txn->get(dbis[Settings], { "hash_seed" }, seed_val)) {
      spdlog::info("Opened database {} for the first time, generating hash seed", filename);
      std::random_device rnd;
      std::mt19937 gen(rnd());
      std::uniform_int_distribution<uint64_t> dist(
        std::numeric_limits<uint64_t>::min(),
        std::numeric_limits<uint64_t>::max()
      );
      seed = dist(gen);
      txn->put(dbis[Settings], { "hash_seed" }, { seed });
    } else {
      spdlog::debug("Loaded existing database {}", filename);
      seed = seed_val.get<uint64_t>();
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

  auto ReadTxn::get_user_id(std::string_view name) -> optional<uint64_t> {
    // TODO: Handle hash collisions, maybe double hashing.
    MDBOutVal v;
    if (ro_txn().get(db.dbis[User_Name], Cursor(name, db.seed).val, v)) return {};
    return { v.get<uint64_t>() };
  }
  auto ReadTxn::get_user(uint64_t id) -> optional<const User*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[User_User], { id }, v)) return {};
    return { GetRoot<User>(v.d_mdbval.mv_data) };
  }
  auto ReadTxn::get_local_user(uint64_t id) -> optional<const LocalUser*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[LocalUser_User], { id }, v)) return {};
    return { GetRoot<LocalUser>(v.d_mdbval.mv_data) };
  }
  auto ReadTxn::count_local_users() -> uint64_t {
    return count(db.dbis[LocalUser_User], ro_txn());
  }
  auto ReadTxn::list_users(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[User_User], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxn::list_local_users(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[LocalUser_User], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxn::list_subscribers(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Subscription_BoardUser],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, 0)),
      { Cursor(board_id, std::numeric_limits<uint64_t>::max()) },
      second_key
    );
  }
  auto ReadTxn::user_is_subscribed(uint64_t user_id, uint64_t board_id) -> bool {
    MDBOutVal v;
    return !ro_txn().get(db.dbis[Subscription_BoardUser], Cursor(user_id, board_id).val, v);
  }

  auto ReadTxn::get_board_id(std::string_view name) -> optional<uint64_t> {
    // TODO: Handle hash collisions, maybe double hashing.
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Board_Name], Cursor(name, db.seed).val, v)) return {};
    return { v.get<uint64_t>() };
  }
  auto ReadTxn::get_board(uint64_t id) -> optional<const Board*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Board_Board], { id }, v)) return {};
    return { GetRoot<Board>(v.d_mdbval.mv_data) };
  }
  auto ReadTxn::get_local_board(uint64_t id) -> optional<const LocalBoard*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[LocalBoard_Board], { id }, v)) return {};
    return { GetRoot<LocalBoard>(v.d_mdbval.mv_data) };
  }
  auto ReadTxn::count_local_boards() -> uint64_t {
    return count(db.dbis[LocalBoard_Board], ro_txn());
  }
  auto ReadTxn::list_boards(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[Board_Board], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxn::list_local_boards(const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(db.dbis[LocalBoard_Board], ro_txn(), cursor, {}, int_key);
  }
  auto ReadTxn::list_subscribed_boards(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Subscription_UserBoard],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, std::numeric_limits<uint64_t>::max()) },
      second_key
    );
  }
  auto ReadTxn::list_created_boards(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Owner_UserBoard],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, std::numeric_limits<uint64_t>::max()) },
      second_key
    );
  }

  auto ReadTxn::get_page(uint64_t id) -> optional<const Page*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Page_Page], { id }, v)) return {};
    return { GetRoot<Page>(v.d_mdbval.mv_data) };
  }
  auto ReadTxn::count_pages_of_board(uint64_t board_id) -> uint64_t {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[PageCount_Board], { board_id }, v)) return 0;
    return v.get<uint64_t>();
  }
  auto ReadTxn::count_pages_of_user(uint64_t user_id) -> uint64_t {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[PageCount_User], { user_id }, v)) return 0;
    return v.get<uint64_t>();
  }
  auto ReadTxn::list_pages_of_board_new(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[PagesNew_BoardTimePage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, 0, 0)),
      { Cursor(board_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_pages_of_board_top(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[PagesTop_BoardKarmaPage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, 0, 0)),
      { Cursor(board_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_pages_of_user_new(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Owner_UserPage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_pages_of_user_top(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[PagesTop_UserKarmaPage],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0, 0)),
      { Cursor(user_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }

  auto ReadTxn::get_note(uint64_t id) -> optional<const Note*> {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Note_Note], { id }, v)) return {};
    return { GetRoot<Note>(v.d_mdbval.mv_data) };
  }
  auto ReadTxn::count_notes_of_post(uint64_t post_id) -> uint64_t {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[DescendantCount_Post], { post_id }, v)) return 0;
    return v.get<uint64_t>();
  }
  auto ReadTxn::count_notes_of_user(uint64_t user_id) -> uint64_t {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[NoteCount_User], { user_id }, v)) return 0;
    return v.get<uint64_t>();
  }
  auto ReadTxn::list_notes_of_post_new(uint64_t post_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[ChildrenNew_PostTimeNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(post_id, 0, 0)),
      { Cursor(post_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_notes_of_post_top(uint64_t post_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[ChildrenTop_PostKarmaNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(post_id, 0, 0)),
      { Cursor(post_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_notes_of_board_new(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[NotesNew_BoardTimeNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, 0, 0)),
      { Cursor(board_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_notes_of_board_top(uint64_t board_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[NotesTop_BoardKarmaNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(board_id, 0, 0)),
      { Cursor(board_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_notes_of_user_new(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[Owner_UserNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0)),
      { Cursor(user_id, std::numeric_limits<uint64_t>::max()) }
    );
  }
  auto ReadTxn::list_notes_of_user_top(uint64_t user_id, const optional<Cursor>& cursor) -> DBIter<uint64_t> {
    return DBIter<uint64_t>(
      db.dbis[NotesTop_UserKarmaNote],
      ro_txn(),
      cursor ? cursor : std::optional(Cursor(user_id, 0, 0)),
      { Cursor(user_id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()) }
    );
  }

  auto ReadTxn::count_karma_of_post(uint64_t post_id) -> int64_t {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Karma_Post], { post_id }, v)) return 0;
    return v.get<int64_t>();
  }
  auto ReadTxn::count_karma_of_user(uint64_t user_id) -> int64_t {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Karma_User], { user_id }, v)) return 0;
    return v.get<int64_t>();
  }
  auto ReadTxn::get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote {
    MDBOutVal v;
    if (ro_txn().get(db.dbis[Vote_UserPost], Cursor(user_id, post_id).val, v)) return NoVote;
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
    MDBOutVal k = { from.val.d_mdbval }, v;
    auto err = cur.first(k, v);
    while (!err) {
      if (mdb_cmp(*txn, dbi, &k.d_mdbval, &to.val.d_mdbval) > 0) return;
      fn(k, v);
      err = cur.del() || cur.next(k, v);
    }
  }
  static inline auto increment(MDBRWTransactionImpl* txn, MDBDbi dbi, uint64_t key) -> void {
    MDBInVal k(key);
    MDBOutVal v;
    if (txn->get(dbi, { k }, v)) txn->put(dbi, { k }, { 1ULL });
    else ++*reinterpret_cast<uint64_t*>(v.d_mdbval.mv_data);
  }
  static inline auto decrement(MDBRWTransactionImpl* txn, MDBDbi dbi, uint64_t key) -> bool {
    MDBInVal k(key);
    MDBOutVal v;
    if (txn->get(dbi, { k }, v)) {
      return true;
    } else {
      uint64_t* ptr = reinterpret_cast<uint64_t*>(v.d_mdbval.mv_data);
      if (*ptr < 1) return true;
      return !--*ptr;
    }
  }
  static inline auto adjust_karma(MDBRWTransactionImpl* txn, MDBDbi dbi, uint64_t key, int64_t diff) -> void {
    MDBInVal k(key);
    MDBOutVal v;
    if (txn->get(dbi, { k }, v)) txn->put(dbi, { k }, { 1LL });
    else *reinterpret_cast<int64_t*>(v.d_mdbval.mv_data) += diff;
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
      spdlog::debug("Updating user {:016x} (name {})", id, user->name()->string_view());
      auto old_user = *old_user_opt;
      if (user->name() != old_user->name()) {
        txn->del(db.dbis[User_Name], Cursor(old_user->name()->string_view(), db.seed).val);
      }
    } else {
      spdlog::debug("Creating user {:016x} (name {})", id, user->name()->string_view());
      txn->put(db.dbis[PageCount_User], id_val, { 0 });
      txn->put(db.dbis[NoteCount_User], id_val, { 0 });
      txn->put(db.dbis[Karma_User], id_val, { 0 });
    }
    txn->put(db.dbis[User_User], id_val, v);
    txn->put(db.dbis[User_Name], Cursor(user->name()->string_view(), db.seed).val, id_val);

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
      spdlog::warn("Tried to delete nonexistent user {:016x}", id);
      return false;
    }

    spdlog::debug("Deleting user {:016x}", id);
    const MDBInVal id_val(id);
    txn->del(db.dbis[User_User], id_val);
    txn->del(db.dbis[User_Name], Cursor((*user_opt)->name()->string_view(), db.seed).val);
    txn->del(db.dbis[LocalUser_User], id_val);
    txn->del(db.dbis[PageCount_User], id_val);
    txn->del(db.dbis[NoteCount_User], id_val);
    txn->del(db.dbis[Karma_User], id_val);

    delete_range(txn.get(), db.dbis[Subscription_UserBoard], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        auto board = c.int_field_1();
        txn->del(db.dbis[Subscription_BoardUser], Cursor(board, c.int_field_0()).val);
        decrement(txn.get(), db.dbis[SubscriberCount_Board], board);
      }
    );
    delete_range(txn.get(), db.dbis[Owner_UserPage], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[Owner_UserNote], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[Owner_UserBoard], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[Owner_UserMedia], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[Owner_UserUrl], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[PagesTop_UserKarmaPage], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[NotesTop_UserKarmaNote], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[Vote_UserPost], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Vote_PostUser], Cursor(c.int_field_1(), c.int_field_0()).val);
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
      spdlog::debug("Updating board {:016x} (name {})", id, board->name()->string_view());
      auto old_board = *old_board_opt;
      if (board->name() != old_board->name()) {
        txn->del(db.dbis[Board_Name], Cursor(old_board->name()->string_view(), db.seed).val);
      }
    } else {
      spdlog::debug("Creating board {:016x} (name {})", id, board->name()->string_view());
      txn->put(db.dbis[PageCount_Board], id_val, { 0 });
      txn->put(db.dbis[NoteCount_Board], id_val, { 0 });
      txn->put(db.dbis[SubscriberCount_Board], id_val, { 0 });
    }
    txn->put(db.dbis[Board_Board], id_val, v);
    txn->put(db.dbis[Board_Name], Cursor(board->name()->string_view(), db.seed).val, id_val);

    // TODO: Handle media (avatar, banner)
  }
  auto WriteTxn::set_local_board(uint64_t id, FlatBufferBuilder&& builder, Offset<LocalBoard> offset) -> void {
    builder.Finish(offset);
    const MDBInVal id_val(id), v({ .d_mdbval = { builder.GetSize(), builder.GetBufferPointer() } });
    auto board = GetRoot<LocalBoard>(builder.GetBufferPointer());
    assert(!!get_user(board->owner()));
    auto old_board_opt = get_local_board(id);
    if (old_board_opt) {
      spdlog::debug("Updating local board {:016x}", id);
      auto old_board = *old_board_opt;
      if (board->owner() != old_board->owner()) {
        spdlog::info("Changing owner of local board {:016x}: {:016x} -> {:016x}", id, old_board->owner(), board->owner());
        txn->del(db.dbis[Owner_UserBoard], Cursor(old_board->owner(), id).val);
      }
    } else {
      spdlog::debug("Updating local board {:016x}", id);
    }
    txn->put(db.dbis[Owner_UserBoard], Cursor(board->owner(), id).val, { board->owner() });
    txn->put(db.dbis[LocalBoard_Board], id_val, v);
  }
  auto WriteTxn::delete_board(uint64_t id) -> bool {
    auto board_opt = get_board(id);
    if (!board_opt) {
      spdlog::warn("Tried to delete nonexistent board {:016x}", id);
      return false;
    }

    spdlog::debug("Deleting board {:016x}", id);
    const MDBInVal id_val(id);
    txn->del(db.dbis[Board_Board], id_val);
    txn->del(db.dbis[Board_Name], Cursor((*board_opt)->name()->string_view(), db.seed).val);
    txn->del(db.dbis[LocalBoard_Board], id_val);
    // TODO: Owner_UserBoard
    txn->del(db.dbis[PageCount_Board], id_val);
    txn->del(db.dbis[NoteCount_Board], id_val);
    txn->del(db.dbis[SubscriberCount_Board], id_val);

    delete_range(txn.get(), db.dbis[Subscription_BoardUser], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Subscription_UserBoard], Cursor(c.int_field_1(), c.int_field_0()).val);
      }
    );
    delete_range(txn.get(), db.dbis[PagesNew_BoardTimePage], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[PagesTop_BoardKarmaPage], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[NotesNew_BoardTimeNote], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[NotesTop_BoardKarmaNote], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));

    return true;
  }
  auto WriteTxn::set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
    MDBOutVal v;
    bool existing = !txn->get(db.dbis[Subscription_BoardUser], Cursor(board_id, user_id).val, v);
    if (subscribed) {
      assert(!!get_user(user_id));
      assert(!!get_board(board_id));
      if (!existing) {
        spdlog::debug("Subscribing user {:016x} to board {:016x}", user_id, board_id);
        auto now = now_ms();
        txn->put(db.dbis[Subscription_BoardUser], Cursor(board_id, user_id).val, { now });
        txn->put(db.dbis[Subscription_UserBoard], Cursor(user_id, board_id).val, { now });
        increment(txn.get(), db.dbis[SubscriberCount_Board], board_id);
      }
    } else if (existing) {
      spdlog::debug("Unsubscribing user {:016x} from board {:016x}", user_id, board_id);
      txn->del(db.dbis[Subscription_BoardUser], Cursor(board_id, user_id).val);
      txn->del(db.dbis[Subscription_UserBoard], Cursor(user_id, board_id).val);
      decrement(txn.get(), db.dbis[SubscriberCount_Board], board_id);
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
    auto karma = karma_uint(count_karma_of_post(id));
    auto old_page_opt = get_page(id);
    assert(!!get_user(page->author()));
    assert(!!get_board(page->board()));
    if (old_page_opt) {
      spdlog::debug("Updating top-level post {:016x} (board {:016x}, author {:016x})", id, page->board(), page->author());
      auto old_page = *old_page_opt;
      assert(page->author() == old_page->author());
      assert(page->created_at() == old_page->created_at());
      if (page->board() != old_page->board()) {
        txn->del(db.dbis[PagesNew_BoardTimePage], Cursor(old_page->board(), page->created_at(), id).val);
        txn->del(db.dbis[PagesTop_BoardKarmaPage], Cursor(old_page->board(), karma, id).val);
        decrement(txn.get(), db.dbis[PageCount_Board], old_page->board());
      }
    } else {
      spdlog::debug("Creating top-level post {:016x} (board {:016x}, author {:016x})", id, page->board(), page->author());
      txn->put(db.dbis[Owner_UserPage], Cursor(page->author(), id).val, { page->author() });
      txn->put(db.dbis[PagesTop_UserKarmaPage], Cursor(page->author(), karma, id).val, id_val);
      txn->put(db.dbis[ChildCount_Post], id_val, { 0ULL });
      txn->put(db.dbis[DescendantCount_Post], id_val, { 0ULL });
      increment(txn.get(), db.dbis[PageCount_User], page->author());
      increment(txn.get(), db.dbis[PageCount_Board], page->board());
    }
    txn->put(db.dbis[Page_Page], id_val, v);
    txn->put(db.dbis[PagesNew_BoardTimePage], Cursor(page->board(), page->created_at(), id).val, id_val);
    txn->put(db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma, id).val, id_val);
    set_vote(page->author(), id, Upvote);
  }
  auto WriteTxn::delete_page(uint64_t id) -> bool {
    auto page_opt = get_page(id);
    if (!page_opt) {
      spdlog::warn("Tried to delete nonexistent top-level post {:016x}", id);
      return false;
    }
    auto page = *page_opt;

    spdlog::debug("Deleting top-level post {:016x} (board {:016x}, author {:016x})", id, page->board(), page->author());
    const MDBInVal id_val(id);
    auto karma = count_karma_of_post(id);
    adjust_karma(txn.get(), db.dbis[Karma_User], page->author(), -karma);
    decrement(txn.get(), db.dbis[PageCount_User], page->author());
    decrement(txn.get(), db.dbis[PageCount_Board], page->board());

    delete_range(txn.get(), db.dbis[Vote_PostUser], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Vote_UserPost], Cursor(c.int_field_1(), c.int_field_0()).val);
      }
    );
    delete_range(txn.get(), db.dbis[ChildrenNew_PostTimeNote], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()),
      [this](MDBOutVal&, MDBOutVal& v) {
        delete_note(v.get<uint64_t>());
      }
    );
    delete_range(txn.get(), db.dbis[ChildrenTop_PostKarmaNote], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[Descendant_PostNote], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()));

    txn->del(db.dbis[Page_Page], id_val);
    txn->del(db.dbis[Owner_UserPage], Cursor(page->author(), id).val);
    txn->del(db.dbis[PagesNew_BoardTimePage], Cursor(page->board(), page->created_at(), id).val);
    txn->del(db.dbis[PagesTop_BoardKarmaPage], Cursor(page->board(), karma_uint(karma), id).val);
    txn->del(db.dbis[ChildCount_Post], id_val);
    txn->del(db.dbis[DescendantCount_Post], id_val);
    txn->del(db.dbis[Karma_Post], id_val);

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
    auto note = GetRoot<Note>(builder.GetBufferPointer());
    auto karma = karma_uint(count_karma_of_post(id));
    auto old_note_opt = get_note(id);
    assert(!!get_user(note->author()));
    if (old_note_opt) {
      spdlog::debug("Updating comment {:016x} (parent {:016x}, author {:016x})", id, note->parent(), note->author());
      auto old_note = *old_note_opt;
      assert(note->author() == old_note->author());
      assert(note->parent() == old_note->parent());
      assert(note->created_at() == old_note->created_at());
    } else {
      spdlog::debug("Creating comment {:016x} (parent {:016x}, author {:016x})", id, note->parent(), note->author());
      txn->put(db.dbis[Owner_UserNote], Cursor(note->author(), id).val, { note->author() });
      txn->put(db.dbis[NotesTop_UserKarmaNote], Cursor(note->author(), karma, id).val, id_val);
      increment(txn.get(), db.dbis[NoteCount_User], note->author());
      increment(txn.get(), db.dbis[ChildCount_Post], note->parent());

      auto ancestor_id = note->parent();
      optional<const Note*> ancestor;
      while ((ancestor = get_note(ancestor_id))) {
        increment(txn.get(), db.dbis[DescendantCount_Post], ancestor_id);
        txn->put(db.dbis[Descendant_PostNote], Cursor(ancestor_id, id).val, id_val);
        ancestor_id = (*ancestor)->parent();
      }
      auto page_ancestor = get_page(ancestor_id);
      assert(!!page_ancestor);
      increment(txn.get(), db.dbis[NoteCount_Board], (*page_ancestor)->board());
      increment(txn.get(), db.dbis[DescendantCount_Post], ancestor_id);
      txn->put(db.dbis[Descendant_PostNote], Cursor(ancestor_id, id).val, id_val);
    }
    txn->put(db.dbis[Note_Note], id_val, v);
    txn->put(db.dbis[ChildrenNew_PostTimeNote], Cursor(note->parent(), note->created_at(), id).val, id_val);
    txn->put(db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma, id).val, id_val);
    set_vote(note->author(), id, Upvote);
  }
  auto WriteTxn::delete_note(uint64_t id) -> bool {
    auto note_opt = get_note(id);
    if (!note_opt) {
      spdlog::warn("Tried to delete nonexistent comment {:016x}", id);
      return false;
    }
    auto note = *note_opt;

    spdlog::debug("Deleting comment {:016x} (parent {:016x}, author {:016x})", id, note->parent(), note->author());
    const MDBInVal id_val(id);
    auto karma = count_karma_of_post(id);
    adjust_karma(txn.get(), db.dbis[Karma_User], note->author(), -karma);
    decrement(txn.get(), db.dbis[NoteCount_User], note->author());

    auto ancestor_id = note->parent();
    for (auto ancestor = note_opt; ancestor; ancestor = get_note(ancestor_id)) {
      ancestor_id = (*ancestor)->parent();
      decrement(txn.get(), db.dbis[ChildCount_Post], ancestor_id);
      decrement(txn.get(), db.dbis[DescendantCount_Post], ancestor_id);
      txn->del(db.dbis[Descendant_PostNote], Cursor(ancestor_id, id).val);
    }
    auto page_ancestor = get_page(ancestor_id);
    if (page_ancestor) {
      auto board = (*page_ancestor)->board();
      txn->del(db.dbis[NotesNew_BoardTimeNote], Cursor(board, note->created_at(), id).val);
      txn->del(db.dbis[NotesTop_BoardKarmaNote], Cursor(board, karma_uint(karma), id).val);
      decrement(txn.get(), db.dbis[NoteCount_Board], board);
    } else {
      spdlog::warn("Deleted comment {:016x} appears to have been orphaned; cannot determine top-level post or board", id);
    }

    delete_range(txn.get(), db.dbis[Vote_PostUser], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()),
      [this](MDBOutVal& k, MDBOutVal&) {
        Cursor c(k);
        txn->del(db.dbis[Vote_UserPost], Cursor(c.int_field_1(), c.int_field_0()).val);
      }
    );
    delete_range(txn.get(), db.dbis[ChildrenNew_PostTimeNote], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()),
      [this](MDBOutVal&, MDBOutVal& v) {
        delete_note(v.get<uint64_t>());
      }
    );
    delete_range(txn.get(), db.dbis[ChildrenTop_PostKarmaNote], Cursor(id, 0, 0), Cursor(id, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()));
    delete_range(txn.get(), db.dbis[Descendant_PostNote], Cursor(id, 0), Cursor(id, std::numeric_limits<uint64_t>::max()));

    txn->del(db.dbis[Note_Note], id_val);
    txn->del(db.dbis[Owner_UserNote], Cursor(note->author(), id).val);
    txn->del(db.dbis[ChildrenNew_PostTimeNote], Cursor(note->parent(), note->created_at(), id).val);
    txn->del(db.dbis[ChildrenTop_PostKarmaNote], Cursor(note->parent(), karma_uint(karma), id).val);
    txn->del(db.dbis[ChildCount_Post], id_val);
    txn->del(db.dbis[DescendantCount_Post], id_val);
    txn->del(db.dbis[Karma_Post], id_val);

    return true;
  }

  auto WriteTxn::set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    const auto existing = get_vote_of_user_for_post(user_id, post_id);
    const int64_t diff = existing - vote;
    if (!diff) return;
    const auto page = get_page(post_id);
    const auto note = page ? std::nullopt : get_note(post_id);
    assert(page || note);
    auto op_id = page ? (*page)->author() : (*note)->author();
    const auto op = get_user(op_id);
    assert(!!op);
    spdlog::debug("Setting vote from user {:016x} on post {:016x} to {}", user_id, post_id, (int8_t)vote);
    if (vote) {
      txn->put(db.dbis[Vote_UserPost], Cursor(user_id, post_id).val, { (int8_t)vote });
      txn->put(db.dbis[Vote_PostUser], Cursor(post_id, user_id).val, { (int8_t)vote });
    } else {
      txn->del(db.dbis[Vote_UserPost], Cursor(user_id, post_id).val);
      txn->del(db.dbis[Vote_PostUser], Cursor(post_id, user_id).val);
    }
    adjust_karma(txn.get(), db.dbis[Karma_Post], post_id, diff);
    adjust_karma(txn.get(), db.dbis[Karma_User], user_id, diff);
  }
}
