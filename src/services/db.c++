#include "services/db.h++"
#include <random>
#include <span>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <duthomhas/csprng.hpp>
#include <openssl/pem.h>
#include "util/lambda_macros.h++"

using std::function, std::min, std::nullopt, std::runtime_error, std::optional,
    std::pair, std::shared_ptr, std::string, std::string_view, std::unique_ptr,
    std::vector, flatbuffers::FlatBufferBuilder, flatbuffers::span,
    flatbuffers::Verifier, flatbuffers::GetRoot;

#define assert_fmt(CONDITION, ...) if (!(CONDITION)) { spdlog::critical(__VA_ARGS__); throw runtime_error("Assertion failed: " #CONDITION); }

namespace Ludwig {
  enum Dbi {
    Settings,
    Session_Session,

    User_User,
    User_Name,
    User_Email,
    UserStats_User,
    LocalUser_User,
    Application_User,
    InvitesOwned_User,
    BoardsOwned_User,
    ThreadsOwned_User,
    CommentsOwned_User,
    MediaOwned_User,
    ThreadsTop_UserKarma,
    ThreadsNew_UserTime,
    CommentsTop_UserKarma,
    CommentsNew_UserTime,
    UpvotePost_User,
    DownvotePost_User,
    PostsSaved_User,
    PostsHidden_User,
    UsersHidden_User,
    BoardsHidden_User,
    BoardsSubscribed_User,
    UsersNew_Time,
    UsersNewPosts_Time,
    UsersMostPosts_Posts,

    Board_Board,
    Board_Name,
    BoardStats_Board,
    LocalBoard_Board,
    ThreadsTop_BoardKarma,
    ThreadsNew_BoardTime,
    ThreadsMostComments_BoardComments,
    CommentsTop_BoardKarma,
    CommentsNew_BoardTime,
    CommentsMostComments_BoardComments,
    UsersSubscribed_Board,
    BoardsNew_Time,
    BoardsNewPosts_Time,
    BoardsMostPosts_Posts,
    BoardsMostSubscribers_Subscribers,

    Thread_Thread,
    Comment_Comment,
    PostStats_Post,
    ChildrenNew_PostTime,
    ChildrenTop_PostKarma,
    MediaInPost_Post,
    ThreadsNew_Time,
    ThreadsTop_Karma,
    ThreadsMostComments_Comments,
    CommentsNew_Time,
    CommentsTop_Karma,
    CommentsMostComments_Comments,

    Invite_Invite,
    Media_Media,
    PostsContaining_Media,

    LinkCard_Url,
    ThreadsByDomain_Domain,

    DBI_MAX
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

  static inline auto db_has(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v) -> bool {
    MDB_cursor* cur;
    if (mdb_cursor_open(txn, dbi, &cur)) return false;
    MDB_val kval = k.val(), vval{ sizeof(uint64_t), &v };
    const bool exists = !mdb_cursor_get(cur, &kval, &vval, MDB_GET_BOTH);
    mdb_cursor_close(cur);
    return exists;
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, MDB_val& k, MDB_val& v, unsigned flags = 0) -> void {
    if (auto err = mdb_put(txn, dbi, &k, &v, flags)) throw DBError("Write failed", err);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, MDB_val& v, unsigned flags = 0) -> void {
    MDB_val kval{ k.length(), const_cast<char*>(k.data()) };
    db_put(txn, dbi, kval, v, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, string_view v, unsigned flags = 0) -> void {
    MDB_val kval{ k.length(), const_cast<char*>(k.data()) };
    MDB_val vval{ v.length(), const_cast<char*>(v.data()) };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, span<uint8_t> v, unsigned flags = 0) -> void {
    MDB_val kval{ k.length(), const_cast<char*>(k.data()) };
    MDB_val vval{ v.size(), v.data() };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, uint64_t v, unsigned flags = 0) -> void {
    MDB_val kval{ k.length(), const_cast<char*>(k.data()) }, vval{ sizeof(uint64_t), &v };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v, unsigned flags = 0) -> void {
    MDB_val kval = k.val(), vval{ sizeof(uint64_t), &v };
    db_put(txn, dbi, kval, vval, flags);
  }

  static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, uint64_t k, span<uint8_t> span, unsigned flags = 0) -> void {
    MDB_val kval{ sizeof(uint64_t), &k }, vval{ span.size(), span.data() };
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

  static inline auto db_del(MDB_txn* txn, MDB_dbi dbi, string_view k) -> void {
    MDB_val kval{ k.length(), const_cast<char*>(k.data()) };
    if (auto err = mdb_del(txn, dbi, &kval, nullptr)) {
      if (err != MDB_NOTFOUND) throw DBError("Delete failed", err);
    }
  }

  static inline auto db_del(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v) -> void {
    MDB_val kval = k.val(), vval{ sizeof(uint64_t), &v };
    if (auto err = mdb_del(txn, dbi, &kval, &vval)) {
      if (err != MDB_NOTFOUND) throw DBError("Delete failed", err);
    }
  }

  static inline auto db_del(MDB_txn* txn, MDB_dbi dbi, string_view k, uint64_t v) -> void {
    MDB_val kval{ k.length(), const_cast<char*>(k.data()) }, vval{ sizeof(uint64_t), &v };
    if (auto err = mdb_del(txn, dbi, &kval, &vval)) {
      if (err != MDB_NOTFOUND) throw DBError("Delete failed", err);
    }
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
    MK_DBI(User_Name, 0)
    MK_DBI(User_Email, 0)
    MK_DBI(UserStats_User, MDB_INTEGERKEY)
    MK_DBI(LocalUser_User, MDB_INTEGERKEY)
    MK_DBI(Application_User, MDB_INTEGERKEY)
    MK_DBI(InvitesOwned_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(BoardsOwned_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsOwned_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsOwned_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(MediaOwned_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsTop_UserKarma, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsNew_UserTime, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsTop_UserKarma, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsNew_UserTime, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(UpvotePost_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(DownvotePost_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(PostsSaved_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(PostsHidden_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(UsersHidden_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(BoardsHidden_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(BoardsSubscribed_User, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(UsersNew_Time, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(UsersNewPosts_Time, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(UsersMostPosts_Posts, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)

    MK_DBI(Board_Board, MDB_INTEGERKEY)
    MK_DBI(Board_Name, 0)
    MK_DBI(BoardStats_Board, MDB_INTEGERKEY)
    MK_DBI(LocalBoard_Board, MDB_INTEGERKEY)
    MK_DBI(ThreadsTop_BoardKarma, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsNew_BoardTime, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsMostComments_BoardComments, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsTop_BoardKarma, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsNew_BoardTime, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsMostComments_BoardComments, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(UsersSubscribed_Board, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(BoardsNew_Time, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(BoardsNewPosts_Time, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(BoardsMostPosts_Posts, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(BoardsMostSubscribers_Subscribers, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)

    MK_DBI(Thread_Thread, MDB_INTEGERKEY)
    MK_DBI(Comment_Comment, MDB_INTEGERKEY)
    MK_DBI(PostStats_Post, MDB_INTEGERKEY)
    MK_DBI(ChildrenNew_PostTime, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ChildrenTop_PostKarma, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(MediaInPost_Post, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsNew_Time, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsTop_Karma, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(ThreadsMostComments_Comments, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsNew_Time, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsTop_Karma, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
    MK_DBI(CommentsMostComments_Comments, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)

    MK_DBI(Invite_Invite, MDB_INTEGERKEY)
    MK_DBI(Media_Media, MDB_INTEGERKEY)
    MK_DBI(PostsContaining_Media, MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)

    MK_DBI(LinkCard_Url, 0)
    MK_DBI(ThreadsByDomain_Domain, MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP)
#   undef MK_DBI

    return 0;
  }

  static constexpr size_t MiB = 1024 * 1024, DUMP_ENTRY_MAX_SIZE = 4 * MiB;

  DB::DB(const char* filename, size_t map_size_mb) :
    map_size(map_size_mb * MiB - (map_size_mb * MiB) % (size_t)sysconf(_SC_PAGESIZE)) {
    MDB_txn* txn = nullptr;
    int err = init_env(filename, &txn);
    if (err) goto die;

    // Load the secrets, or generate them if missing
    MDB_val val;
    if ((err = db_get(txn, dbis[Settings], SettingsKey::jwt_secret, val))) {
      spdlog::info("Opened database {} for the first time, generating secrets", filename);
      duthomhas::csprng rng;
      uint8_t jwt_secret[JWT_SECRET_SIZE];
      rng(jwt_secret);
      const auto now = now_s();
      db_put(txn, dbis[Settings], SettingsKey::next_id, 1ULL);
      val = { JWT_SECRET_SIZE, jwt_secret };
      db_put(txn, dbis[Settings], SettingsKey::jwt_secret, val);
      db_put(txn, dbis[Settings], SettingsKey::base_url, "http://localhost:2023");
      db_put(txn, dbis[Settings], SettingsKey::created_at, now);
      db_put(txn, dbis[Settings], SettingsKey::updated_at, now);
      db_put(txn, dbis[Settings], SettingsKey::name, "Ludwig");
      db_put(txn, dbis[Settings], SettingsKey::description, "A new Ludwig server");
      db_put(txn, dbis[Settings], SettingsKey::post_max_length, MiB);
      db_put(txn, dbis[Settings], SettingsKey::media_upload_enabled, 0ULL);
      db_put(txn, dbis[Settings], SettingsKey::board_creation_admin_only, 1ULL);
      db_put(txn, dbis[Settings], SettingsKey::federation_enabled, 0ULL);
      db_put(txn, dbis[Settings], SettingsKey::federate_cw_content, 1ULL);
      db_put(txn, dbis[Settings], SettingsKey::infinite_scroll_enabled, 1ULL);
      db_put(txn, dbis[Settings], SettingsKey::javascript_enabled, 1ULL);
    } else {
      spdlog::debug("Loaded existing database {}", filename);
      assert_fmt(val.mv_size == JWT_SECRET_SIZE, "jwt_secret is wrong size: expected {}, got {}", JWT_SECRET_SIZE, val.mv_size);
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

  DB::DB(
    const char* filename,
    function<size_t (uint8_t*, size_t)> read,
    optional<shared_ptr<SearchEngine>> search,
    size_t map_size_mb
  ) : map_size(map_size_mb * MiB - (map_size_mb * MiB) % (size_t)sysconf(_SC_PAGESIZE)) {
    {
      struct stat stat_buf;
      if (stat(filename, &stat_buf) == 0) {
        throw runtime_error("Cannot import database dump: database file " +
            string(filename) + " already exists and would be overwritten.");
      }
    }
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
    auto buf = std::make_unique<uint8_t[]>(DUMP_ENTRY_MAX_SIZE);
    while (read(buf.get(), 4) == 4) {
      const auto len = flatbuffers::GetSizePrefixedBufferLength(buf.get());
      if (len > DUMP_ENTRY_MAX_SIZE) {
        throw runtime_error(fmt::format("DB dump entry is larger than max of {}MiB", DUMP_ENTRY_MAX_SIZE / MiB));
      } else if (len < 4) {
        throw runtime_error("DB dump entry is less than 4 bytes; this shouldn't be possible");
      } else if (len > 4) {
        const auto bytes = read(buf.get() + 4, len - 4);
        if (bytes != len - 4) {
          throw runtime_error("Did not read the expected number of bytes (truncated DB dump entry?)");
        }
      }
      const auto entry = flatbuffers::GetSizePrefixedRoot<Dump>(buf.get());
      Verifier verifier(buf.get(), len);
      if (!entry->Verify(verifier)) {
        throw runtime_error("FlatBuffer verification failed on read");
      }
      const span<uint8_t> span((uint8_t*)entry->data()->data(), entry->data()->size());
      switch (entry->type()) {
        case DumpType::User:
          txn.set_user(entry->id(), span);
          if (search) (*search)->index(entry->id(), *GetRoot<User>(span.data()));
          break;
        case DumpType::LocalUser:
          txn.set_local_user(entry->id(), span);
          break;
        case DumpType::Board:
          txn.set_board(entry->id(), span);
          if (search) (*search)->index(entry->id(), *GetRoot<Board>(span.data()));
          break;
        case DumpType::LocalBoard:
          txn.set_local_board(entry->id(), span);
          break;
        case DumpType::Thread:
          txn.set_thread(entry->id(), span);
          if (search) (*search)->index(entry->id(), *GetRoot<Thread>(span.data()));
          break;
        case DumpType::Comment:
          txn.set_comment(entry->id(), span);
          if (search) (*search)->index(entry->id(), *GetRoot<Comment>(span.data()));
          break;
        case DumpType::SettingRecord: {
          const auto rec = GetRoot<SettingRecord>(span.data());
          if (rec->value_str()) {
            txn.set_setting(rec->key()->string_view(), rec->value_str()->string_view());
          } else {
            txn.set_setting(rec->key()->string_view(), rec->value_int().value_or(0));
          }
          break;
        }
        case DumpType::UpvoteBatch: {
          const auto batch = GetRoot<VoteBatch>(span.data());
          for (const auto post : *batch->posts()) {
            txn.set_vote(entry->id(), post, Vote::Upvote);
          }
          break;
        }
        case DumpType::DownvoteBatch: {
          const auto batch = GetRoot<VoteBatch>(span.data());
          for (const auto post : *batch->posts()) {
            txn.set_vote(entry->id(), post, Vote::Downvote);
          }
          break;
        }
        case DumpType::SubscriptionBatch: {
          const auto batch = GetRoot<SubscriptionBatch>(span.data());
          for (const auto board : *batch->boards()) {
            txn.set_subscription(entry->id(), board, true);
          }
          break;
        }
        default:
          throw runtime_error("Invalid entry in database dump");
      }
    }
    txn.commit();
    on_error.canceled = true;
  }

  DB::DB(DB&& from) : map_size(from.map_size), env(from.env) {
    memcpy(dbis, from.dbis, sizeof(dbis));
    from.env = nullptr;
  }

  auto DB::operator=(DB&& from) -> DB& {
    map_size = from.map_size;
    env = from.env;
    from.env = nullptr;
    memcpy(dbis, from.dbis, sizeof(dbis));
    return *this;
  }

  DB::~DB() {
    if (env != nullptr) mdb_env_close(env);
  }

  template <typename T> static inline auto get_fb(const span<uint8_t>& span) -> const T& {
    const auto& root = *GetRoot<T>(span.data());
    Verifier verifier(span.data(), span.size());
    if (!root.Verify(verifier)) {
      throw runtime_error("FlatBuffer verification failed on write");
    }
    return root;
  }

  template <typename T> static inline auto get_fb(const MDB_val& v) -> const T& {
    const auto& root = *GetRoot<T>(v.mv_data);
    Verifier verifier((const uint8_t*)v.mv_data, v.mv_size);
    if (!root.Verify(verifier)) {
      throw runtime_error("FlatBuffer verification failed on read (corrupt data!)");
    }
    return root;
  }

  static inline auto count(MDB_dbi dbi, MDB_txn* txn, optional<Cursor> from = {}, optional<Cursor> to = {}) -> uint64_t {
    DBIter iter(dbi, txn, Dir::Asc, from, to);
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
    if (db_get(txn, db.dbis[Settings], key, v)) return 0;
    return val_as<uint64_t>(v);
  }
  auto ReadTxnBase::get_jwt_secret() -> JwtSecret {
    MDB_val v;
    if (auto err = db_get(txn, db.dbis[Settings], SettingsKey::jwt_secret, v)) throw DBError("jwt_secret error", err);
    return std::span<uint8_t, JWT_SECRET_SIZE>((uint8_t*)v.mv_data, v.mv_size);
  }
  auto ReadTxnBase::get_public_key() -> unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)> {
    MDB_val v;
    if (auto err = db_get(txn, db.dbis[Settings], SettingsKey::public_key, v)) throw DBError("public_key error", err);
    const auto bio = unique_ptr<BIO, int(*)(BIO*)>(BIO_new_mem_buf(v.mv_data, (ssize_t)v.mv_size), BIO_free);
    auto* k = PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr);
    if (k == nullptr) throw runtime_error("public_key is not valid");
    return unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)>(k, EVP_PKEY_free);
  }
  auto ReadTxnBase::get_private_key() -> unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)> {
    MDB_val v;
    if (auto err = db_get(txn, db.dbis[Settings], SettingsKey::private_key, v)) throw DBError("private_key error", err);
    const auto bio = unique_ptr<BIO, int(*)(BIO*)>(BIO_new_mem_buf(v.mv_data, (ssize_t)v.mv_size), BIO_free);
    auto* k = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
    if (k == nullptr) throw runtime_error("private_key is not valid");
    return unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)>(k, EVP_PKEY_free);
  }
  auto ReadTxnBase::get_site_stats() -> const SiteStats& {
    MDB_val v;
    if (db_get(txn, db.dbis[Settings], SettingsKey::site_stats, v)) {
      static std::atomic<bool> initialized = false;
      static FlatBufferBuilder fbb;
      bool expected = false;
      if (initialized.compare_exchange_strong(expected, true)) {
        fbb.Finish(CreateSiteStats(fbb, 0, 0, 0, 0));
      }
      return *GetRoot<SiteStats>(fbb.GetBufferPointer());
    }
    return get_fb<SiteStats>(v);
  }
  auto ReadTxnBase::get_admin_list() -> span<uint64_t> {
    const auto s = get_setting_str(SettingsKey::admins);
    return span<uint64_t>((uint64_t*)s.data(), s.length() / sizeof(uint64_t));
  }
  auto ReadTxnBase::get_session(uint64_t session_id) -> OptRef<Session> {
    MDB_val v;
    if (db_get(txn, db.dbis[Session_Session], session_id, v)) {
      spdlog::debug("Session {:x} does not exist", session_id);
      return {};
    }
    const auto& session = get_fb<Session>(v);
    if (session.expires_at() > now_s()) return { session };
    spdlog::debug("Session {:x} is expired", session_id);
    return {};
  }

  auto ReadTxnBase::get_user_id_by_name(string_view name) -> optional<uint64_t> {
    const auto name_lc = to_ascii_lowercase(name);
    MDB_val v;
    if (db_get(txn, db.dbis[User_Name], name_lc, v)) return {};
    return val_as<uint64_t>(v);
  }
  auto ReadTxnBase::get_user_id_by_email(string_view email) -> optional<uint64_t> {
    const auto email_lc = to_ascii_lowercase(email);
    MDB_val v;
    if (db_get(txn, db.dbis[User_Email], email_lc, v)) return {};
    return val_as<uint64_t>(v);
  }
  auto ReadTxnBase::get_user(uint64_t id) -> OptRef<User> {
    MDB_val v;
    if (db_get(txn, db.dbis[User_User], id, v)) return {};
    return get_fb<User>(v);
  }
  auto ReadTxnBase::get_user_stats(uint64_t id) -> OptRef<UserStats> {
    MDB_val v;
    if (db_get(txn, db.dbis[UserStats_User], id, v)) return {};
    return get_fb<UserStats>(v);
  }
  auto ReadTxnBase::get_local_user(uint64_t id) -> OptRef<LocalUser> {
    MDB_val v;
    if (db_get(txn, db.dbis[LocalUser_User], id, v)) return {};
    return get_fb<LocalUser>(v);
  }
  auto ReadTxnBase::count_local_users() -> uint64_t {
    return count(db.dbis[LocalUser_User], txn);
  }
  auto ReadTxnBase::list_users_alphabetical(optional<string_view> cursor) -> DBIter {
    return DBIter(db.dbis[User_Name], txn, Dir::Asc, cursor.transform(λx(MDB_val(x.length(),(void*)x.data()))));
  }
  auto ReadTxnBase::list_users_new(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[UsersNew_Time], txn, Dir::Desc, *cursor);
    return DBIter(db.dbis[UsersNew_Time], txn, Dir::Desc);
  }
  auto ReadTxnBase::list_users_old(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[UsersNew_Time], txn, Dir::Asc, *cursor);
    return DBIter(db.dbis[UsersNew_Time], txn, Dir::Asc);
  }
  auto ReadTxnBase::list_users_new_posts(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[UsersNewPosts_Time], txn, Dir::Desc, *cursor);
    return DBIter(db.dbis[UsersNewPosts_Time], txn, Dir::Desc);
  }
  auto ReadTxnBase::list_users_most_posts(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[UsersMostPosts_Posts], txn, Dir::Desc, *cursor);
    return DBIter(db.dbis[UsersMostPosts_Posts], txn, Dir::Desc);
  }
  auto ReadTxnBase::list_subscribers(uint64_t board_id, OptCursor cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[UsersSubscribed_Board], txn, Dir::Asc, pair(Cursor(board_id), cursor->int_field_0()), Cursor(board_id+1));
    return DBIter(db.dbis[UsersSubscribed_Board], txn, Dir::Asc, optional<MDB_val>(), Cursor(board_id+1));
  }
  auto ReadTxnBase::is_user_subscribed_to_board(uint64_t user_id, uint64_t board_id) -> bool {
    return db_has(txn, db.dbis[UsersSubscribed_Board], board_id, user_id);
  }

  auto ReadTxnBase::get_board_id_by_name(string_view name) -> optional<uint64_t> {
    const auto name_lc = to_ascii_lowercase(name);
    MDB_val v;
    if (db_get(txn, db.dbis[Board_Name], name_lc, v)) return {};
    return val_as<uint64_t>(v);
  }
  auto ReadTxnBase::get_board(uint64_t id) -> OptRef<Board> {
    MDB_val v;
    if (db_get(txn, db.dbis[Board_Board], id, v)) return {};
    return get_fb<Board>(v);
  }
  auto ReadTxnBase::get_board_stats(uint64_t id) -> OptRef<BoardStats> {
    MDB_val v;
    if (db_get(txn, db.dbis[BoardStats_Board], id, v)) return {};
    return get_fb<BoardStats>(v);
  }
  auto ReadTxnBase::get_local_board(uint64_t id) -> OptRef<LocalBoard> {
    MDB_val v;
    if (db_get(txn, db.dbis[LocalBoard_Board], id, v)) return {};
    return get_fb<LocalBoard>(v);
  }
  auto ReadTxnBase::count_local_boards() -> uint64_t {
    return count(db.dbis[LocalBoard_Board], txn);
  }
  auto ReadTxnBase::list_boards_alphabetical(optional<string_view> cursor) -> DBIter {
    return DBIter(db.dbis[Board_Name], txn, Dir::Asc, cursor.transform(λx(MDB_val(x.length(),(void*)x.data()))));
  }
  auto ReadTxnBase::list_boards_new(OptKV cursor) -> DBIter {
    if (cursor) DBIter(db.dbis[BoardsNew_Time], txn, Dir::Desc, *cursor);
    return DBIter(db.dbis[BoardsNew_Time], txn, Dir::Desc);
  }
  auto ReadTxnBase::list_boards_old(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[BoardsNew_Time], txn, Dir::Asc, *cursor);
    return DBIter(db.dbis[BoardsNew_Time], txn, Dir::Asc);
  }
  auto ReadTxnBase::list_boards_new_posts(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[BoardsNewPosts_Time], txn, Dir::Desc, *cursor);
    return DBIter(db.dbis[BoardsNewPosts_Time], txn, Dir::Desc);
  }
  auto ReadTxnBase::list_boards_most_posts(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[BoardsMostPosts_Posts], txn, Dir::Desc, *cursor);
    return DBIter(db.dbis[BoardsMostPosts_Posts], txn, Dir::Desc);
  }
  auto ReadTxnBase::list_boards_most_subscribers(OptKV cursor) -> DBIter {
    if (cursor) return DBIter(db.dbis[BoardsMostSubscribers_Subscribers], txn, Dir::Desc, *cursor);
    return DBIter(db.dbis[BoardsMostSubscribers_Subscribers], txn, Dir::Desc);
  }
  auto ReadTxnBase::list_subscribed_boards(uint64_t user_id, OptCursor cursor) -> DBIter {
    return DBIter(
      db.dbis[BoardsSubscribed_User],
      txn,
      Dir::Asc,
      pair(Cursor(user_id), cursor ? cursor->int_field_0() : 0),
      Cursor(user_id + 1)
    );
  }
  auto ReadTxnBase::list_created_boards(uint64_t user_id, OptCursor cursor) -> DBIter {
    return DBIter(
      db.dbis[BoardsOwned_User],
      txn,
      Dir::Asc,
      pair(Cursor(user_id), cursor ? cursor->int_field_0() : 0),
      Cursor(user_id + 1)
    );
  }

  auto ReadTxnBase::get_post_stats(uint64_t id) -> OptRef<PostStats> {
    MDB_val v;
    if (db_get(txn, db.dbis[PostStats_Post], id, v)) return {};
    return get_fb<PostStats>(v);
  }
  auto ReadTxnBase::get_thread(uint64_t id) -> OptRef<Thread> {
    MDB_val v;
    if (db_get(txn, db.dbis[Thread_Thread], id, v)) return {};
    return get_fb<Thread>(v);
  }
  auto ReadTxnBase::list_threads_new(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsNew_Time],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(ID_MAX), ID_MAX)),
      Cursor(0)
    );
  }
  auto ReadTxnBase::list_threads_old(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsNew_Time],
      txn,
      Dir::Asc,
      cursor.value_or(pair(Cursor(0), 0)),
      Cursor(ID_MAX)
    );
  }
  auto ReadTxnBase::list_threads_top(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsTop_Karma],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(ID_MAX), ID_MAX)),
      Cursor(0)
    );
  }
  auto ReadTxnBase::list_threads_most_comments(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsMostComments_Comments],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(ID_MAX), ID_MAX)),
      Cursor(0)
    );
  }
  auto ReadTxnBase::list_threads_of_board_new(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsNew_BoardTime],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(board_id, ID_MAX), ID_MAX)),
      Cursor(board_id, 0)
    );
  }
  auto ReadTxnBase::list_threads_of_board_old(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsNew_BoardTime],
      txn,
      Dir::Asc,
      cursor.value_or(pair(Cursor(board_id, 0), 0)),
      Cursor(board_id, ID_MAX)
    );
  }
  auto ReadTxnBase::list_threads_of_board_top(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsTop_BoardKarma],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(board_id, ID_MAX), ID_MAX)),
      Cursor(board_id, 0)
    );
  }
  auto ReadTxnBase::list_threads_of_board_most_comments(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsMostComments_BoardComments],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(board_id, ID_MAX), ID_MAX)),
      Cursor(board_id, 0)
    );
  }
  auto ReadTxnBase::list_threads_of_user_new(uint64_t user_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsNew_UserTime],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(user_id, ID_MAX), ID_MAX)),
      Cursor(user_id, 0)
    );
  }
  auto ReadTxnBase::list_threads_of_user_old(uint64_t user_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsNew_UserTime],
      txn,
      Dir::Asc,
      cursor.value_or(pair(Cursor(user_id, 0), 0)),
      Cursor(user_id, ID_MAX)
    );
  }
  auto ReadTxnBase::list_threads_of_user_top(uint64_t user_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ThreadsTop_UserKarma],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(user_id, ID_MAX), ID_MAX)),
      Cursor(user_id, 0)
    );
  }

  auto ReadTxnBase::get_comment(uint64_t id) -> OptRef<Comment> {
    MDB_val v;
    if (db_get(txn, db.dbis[Comment_Comment], id, v)) return {};
    return get_fb<Comment>(v);
  }
  auto ReadTxnBase::list_comments_new(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsNew_Time],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(ID_MAX), ID_MAX)),
      Cursor(0)
    );
  }
  auto ReadTxnBase::list_comments_old(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsNew_Time],
      txn,
      Dir::Asc,
      cursor.value_or(pair(Cursor(0), 0)),
      Cursor(ID_MAX)
    );
  }
  auto ReadTxnBase::list_comments_top(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsTop_Karma],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(ID_MAX), ID_MAX)),
      Cursor(0)
    );
  }
  auto ReadTxnBase::list_comments_most_comments(OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsMostComments_Comments],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(ID_MAX), ID_MAX)),
      Cursor(0)
    );
  }
  auto ReadTxnBase::list_comments_of_post_new(uint64_t post_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ChildrenNew_PostTime],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(post_id, ID_MAX), ID_MAX)),
      Cursor(post_id, 0)
    );
  }
  auto ReadTxnBase::list_comments_of_post_old(uint64_t post_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ChildrenNew_PostTime],
      txn,
      Dir::Asc,
      cursor.value_or(pair(Cursor(post_id, 0), 0)),
      Cursor(post_id, ID_MAX)
    );
  }
  auto ReadTxnBase::list_comments_of_post_top(uint64_t post_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[ChildrenTop_PostKarma],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(post_id, ID_MAX), ID_MAX)),
      Cursor(post_id, 0)
    );
  }
  auto ReadTxnBase::list_comments_of_board_new(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsNew_BoardTime],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(board_id, ID_MAX), ID_MAX)),
      Cursor(board_id, 0)
    );
  }
  auto ReadTxnBase::list_comments_of_board_old(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsNew_BoardTime],
      txn,
      Dir::Asc,
      cursor.value_or(pair(Cursor(board_id, 0), 0)),
      Cursor(board_id, ID_MAX)
    );
  }
  auto ReadTxnBase::list_comments_of_board_top(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsTop_BoardKarma],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(board_id, ID_MAX), ID_MAX)),
      Cursor(board_id, 0)
    );
  }
  auto ReadTxnBase::list_comments_of_board_most_comments(uint64_t board_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsMostComments_BoardComments],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(board_id, ID_MAX), ID_MAX)),
      Cursor(board_id, 0)
    );
  }
  auto ReadTxnBase::list_comments_of_user_new(uint64_t user_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsNew_UserTime],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(user_id, ID_MAX), ID_MAX)),
      Cursor(user_id, 0)
    );
  }
  auto ReadTxnBase::list_comments_of_user_old(uint64_t user_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsNew_UserTime],
      txn,
      Dir::Asc,
      cursor.value_or(pair(Cursor(user_id, 0), 0)),
      Cursor(user_id, ID_MAX)
    );
  }
  auto ReadTxnBase::list_comments_of_user_top(uint64_t user_id, OptKV cursor) -> DBIter {
    return DBIter(
      db.dbis[CommentsTop_UserKarma],
      txn,
      Dir::Desc,
      cursor.value_or(pair(Cursor(user_id, ID_MAX), ID_MAX)),
      Cursor(user_id, 0)
    );
  }

  auto ReadTxnBase::get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote {
    if (db_has(txn, db.dbis[UpvotePost_User], user_id, post_id)) return Vote::Upvote;
    if (db_has(txn, db.dbis[DownvotePost_User], user_id, post_id)) return Vote::Downvote;
    return Vote::NoVote;
  }

  auto ReadTxnBase::has_user_saved_post(uint64_t user_id, uint64_t post_id) -> bool {
    return db_has(txn, db.dbis[PostsSaved_User], user_id, post_id);
  }
  auto ReadTxnBase::has_user_hidden_post(uint64_t user_id, uint64_t post_id) -> bool {
    return db_has(txn, db.dbis[PostsHidden_User], user_id, post_id);
  }
  auto ReadTxnBase::has_user_hidden_user(uint64_t user_id, uint64_t hidden_user_id) -> bool {
    return db_has(txn, db.dbis[UsersHidden_User], user_id, hidden_user_id);
  }
  auto ReadTxnBase::has_user_hidden_board(uint64_t user_id, uint64_t board_id) -> bool {
    return db_has(txn, db.dbis[BoardsHidden_User], user_id, board_id);
  }

  auto ReadTxnBase::get_application(uint64_t user_id) -> OptRef<Application> {
    MDB_val v;
    if (db_get(txn, db.dbis[Application_User], user_id, v)) return {};
    return get_fb<Application>(v);
  }
  auto ReadTxnBase::list_applications(OptCursor cursor) -> DBIter {
    return DBIter(
      db.dbis[Application_User],
      txn,
      Dir::Asc,
      cursor.value_or(Cursor(0)),
      Cursor(ID_MAX)
    );
  }

  auto ReadTxnBase::get_invite(uint64_t invite_id) -> OptRef<Invite> {
    MDB_val v;
    if (db_get(txn, db.dbis[Invite_Invite], invite_id, v)) return {};
    return get_fb<Invite>(v);
  }
  auto ReadTxnBase::list_invites_from_user(uint64_t user_id, OptCursor cursor) -> DBIter {
    return DBIter(
      db.dbis[Application_User],
      txn,
      Dir::Desc,
      cursor.value_or(Cursor(user_id, ID_MAX)),
      Cursor(user_id, 0)
    );
  }

  auto ReadTxnBase::get_link_card(std::string_view url) -> OptRef<LinkCard> {
    MDB_val v;
    if (db_get(txn, db.dbis[LinkCard_Url], url, v)) return {};
    return get_fb<LinkCard>(v);
  }

  static inline auto delete_range(
    MDB_txn* txn,
    MDB_dbi dbi,
    Cursor from,
    Cursor to,
    const function<void(MDB_val& k, MDB_val& v)>& fn = [](MDB_val&, MDB_val&){}
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

  auto ReadTxnBase::dump(uWS::MoveOnlyFunction<void (const flatbuffers::span<uint8_t>&, bool)> on_data) -> void {
    FlatBufferBuilder fbb, fbb2;
    MDB_val k, v, v2;
    MDB_cursor* cur;
    // Settings
    int err = mdb_cursor_open(txn, db.dbis[Settings], &cur);
    bool first = true;
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT)) {
      if (!SettingsKey::is_exported(string_view{(const char*)k.mv_data, k.mv_size})) continue;
      if (first) first = false;
      else {
        on_data(fbb.GetBufferSpan(), false);
        fbb.Clear();
      }
      fbb2.Finish(CreateSettingRecord(fbb2, fbb2.CreateString((const char*)k.mv_data, k.mv_size), 0, fbb2.CreateString((const char*)v.mv_data, v.mv_size)));
      fbb.FinishSizePrefixed(CreateDump(fbb, 0, DumpType::SettingRecord, fbb.CreateVector(fbb2.GetBufferPointer(), fbb2.GetSize())));
      fbb2.Clear();
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: settings)", err);
    mdb_cursor_close(cur);
    // Users
    err = mdb_cursor_open(txn, db.dbis[User_User], &cur);
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT)) {
      on_data(fbb.GetBufferSpan(), false);
      fbb.Clear();
      fbb.FinishSizePrefixed(CreateDump(fbb, val_as<uint64_t>(k), DumpType::User, fbb.CreateVector((uint8_t*)v.mv_data, v.mv_size)));
      err = mdb_get(txn, db.dbis[LocalUser_User], &k, &v2);
      if (!err) {
        on_data(fbb.GetBufferSpan(), false);
        fbb.Clear();
        fbb.FinishSizePrefixed(CreateDump(fbb, val_as<uint64_t>(k), DumpType::LocalUser, fbb.CreateVector((uint8_t*)v2.mv_data, v2.mv_size)));
      } else if (err == MDB_NOTFOUND) err = 0;
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: users)", err);
    mdb_cursor_close(cur);
    // Boards
    err = mdb_cursor_open(txn, db.dbis[Board_Board], &cur);
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT)) {
      on_data(fbb.GetBufferSpan(), false);
      fbb.Clear();
      fbb.FinishSizePrefixed(CreateDump(fbb, val_as<uint64_t>(k), DumpType::Board, fbb.CreateVector((uint8_t*)v.mv_data, v.mv_size)));
      err = mdb_get(txn, db.dbis[LocalBoard_Board], &k, &v2);
      if (!err) {
        on_data(fbb.GetBufferSpan(), false);
        fbb.Clear();
        fbb.FinishSizePrefixed(CreateDump(fbb, val_as<uint64_t>(k), DumpType::LocalBoard, fbb.CreateVector((uint8_t*)v2.mv_data, v2.mv_size)));
      } else if (err == MDB_NOTFOUND) err = 0;
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: boards)", err);
    mdb_cursor_close(cur);
    // Threads
    err = mdb_cursor_open(txn, db.dbis[Thread_Thread], &cur);
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT)) {
      on_data(fbb.GetBufferSpan(), false);
      fbb.Clear();
      fbb.FinishSizePrefixed(CreateDump(fbb, val_as<uint64_t>(k), DumpType::Thread, fbb.CreateVector((uint8_t*)v.mv_data, v.mv_size)));
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: threads)", err);
    mdb_cursor_close(cur);
    // Comments
    err = mdb_cursor_open(txn, db.dbis[Comment_Comment], &cur);
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT)) {
      on_data(fbb.GetBufferSpan(), false);
      fbb.Clear();
      fbb.FinishSizePrefixed(CreateDump(fbb, val_as<uint64_t>(k), DumpType::Comment, fbb.CreateVector((uint8_t*)v.mv_data, v.mv_size)));
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: comments)", err);
    mdb_cursor_close(cur);
    // Votes
    spdlog::info("in votes step");
    err = mdb_cursor_open(txn, db.dbis[UpvotePost_User], &cur);
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT_NODUP)) {
      for (err = mdb_cursor_get(cur, &k, &v, MDB_GET_MULTIPLE); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT_MULTIPLE)) {
        on_data(fbb.GetBufferSpan(), false);
        spdlog::info("upvote batch of {}", v.mv_size / sizeof(uint64_t));
        fbb.Clear();
        fbb2.Finish(CreateVoteBatch(fbb2, fbb2.CreateVector((uint64_t*)v.mv_data, v.mv_size / sizeof(uint64_t))));
        fbb.FinishSizePrefixed(
          CreateDump(fbb, val_as<uint64_t>(k), DumpType::UpvoteBatch, fbb.CreateVector(fbb2.GetBufferPointer(), fbb2.GetSize()))
        );
        fbb2.Clear();
      }
      if (err != MDB_NOTFOUND) throw DBError("Export failed (step: upvotes)", err);
      err = 0;
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: upvotes)", err);
    mdb_cursor_close(cur);
    err = mdb_cursor_open(txn, db.dbis[DownvotePost_User], &cur);
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT_NODUP)) {
      for (err = mdb_cursor_get(cur, &k, &v, MDB_GET_MULTIPLE); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT_MULTIPLE)) {
        on_data(fbb.GetBufferSpan(), false);
        spdlog::info("downvote batch of {}", v.mv_size / sizeof(uint64_t));
        fbb.Clear();
        fbb2.Finish(CreateVoteBatch(fbb2, fbb2.CreateVector((uint64_t*)v.mv_data, v.mv_size / sizeof(uint64_t))));
        fbb.FinishSizePrefixed(
          CreateDump(fbb, val_as<uint64_t>(k), DumpType::DownvoteBatch, fbb.CreateVector(fbb2.GetBufferPointer(), fbb2.GetSize()))
        );
        fbb2.Clear();
      }
      if (err != MDB_NOTFOUND) throw DBError("Export failed (step: downvotes)", err);
      err = 0;
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: downvotes)", err);
    mdb_cursor_close(cur);
    // Subscriptions
    err = mdb_cursor_open(txn, db.dbis[BoardsSubscribed_User], &cur);
    for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT_NODUP)) {
      for (err = mdb_cursor_get(cur, &k, &v, MDB_GET_MULTIPLE); !err; err = mdb_cursor_get(cur, &k, &v, MDB_NEXT_MULTIPLE)) {
        on_data(fbb.GetBufferSpan(), false);
        fbb.Clear();
        fbb2.Finish(CreateSubscriptionBatch(fbb2, fbb2.CreateVector((uint64_t*)v.mv_data, v.mv_size / sizeof(uint64_t))));
        fbb.FinishSizePrefixed(
          CreateDump(fbb, val_as<uint64_t>(k), DumpType::SubscriptionBatch, fbb.CreateVector(fbb2.GetBufferPointer(), fbb2.GetSize()))
        );
        fbb2.Clear();
      }
      if (err != MDB_NOTFOUND) throw DBError("Export failed (step: subscriptions)", err);
      err = 0;
    }
    if (err != MDB_NOTFOUND) throw DBError("Export failed (step: subscriptions)", err);
    mdb_cursor_close(cur);
    on_data(fbb.GetBufferSpan(), true);
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
  ) -> pair<uint64_t, uint64_t> {
    uint64_t id, now = now_s();
    if (!(++db.session_counter % 4)) {
      // Every 4 sessions, clean up old sessions.
      // TODO: Change this to 256; the low number is for testing.
      MDBCursor cur(txn, db.dbis[Session_Session]);
      MDB_val k, v;
      int err;
      for (err = mdb_cursor_get(cur, &k, &v, MDB_FIRST); !err; err = err || mdb_cursor_get(cur, &k, &v, MDB_NEXT)) {
        auto& session = get_fb<Session>(v);
        if (session.expires_at() <= now) {
          spdlog::debug("Deleting expired session {:x} for user {:x}", val_as<uint64_t>(k), session.user());
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
    db_put(txn, db.dbis[Session_Session], id, fbb.GetBufferSpan());
    spdlog::debug("Created session {:x} for user {:x} (IP {}, user agent {})", id, user, ip, user_agent);
    return { id, now + lifetime_seconds };
  }
  auto WriteTxn::delete_session(uint64_t session_id) -> void {
    db_del(txn, db.dbis[Session_Session], session_id);
  }
  auto WriteTxn::create_user(span<uint8_t> span) -> uint64_t {
    const uint64_t id = next_id();
    set_user(id, span);
    return id;
  }
  auto WriteTxn::set_user(uint64_t id, span<uint8_t> span) -> void {
    const auto& user = get_fb<User>(span);
    if (const auto old_user_opt = get_user(id)) {
      spdlog::debug("Updating user {:x} (name {})", id, user.name()->string_view());
      const auto& old_user = old_user_opt->get();
      if (user.name()->string_view() != old_user.name()->string_view()) {
        db_del(txn, db.dbis[User_Name], old_user.name()->string_view());
      }
    } else {
      spdlog::debug("Creating user {:x} (name {})", id, user.name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateUserStats(fbb));
      db_put(txn, db.dbis[UserStats_User], id, fbb.GetBufferSpan());
    }
    db_put(txn, db.dbis[User_Name], user.name()->string_view(), id);
    db_put(txn, db.dbis[User_User], id, span);
    db_put(txn, db.dbis[UsersNew_Time], user.created_at(), id);
    db_put(txn, db.dbis[UsersNewPosts_Time], Cursor(0), id);
    db_put(txn, db.dbis[UsersMostPosts_Posts], Cursor(0), id);
  }
  auto WriteTxn::set_local_user(uint64_t id, span<uint8_t> span) -> void {
    const auto& user = get_fb<LocalUser>(span);
    const auto old_user_opt = get_local_user(id);
    if (old_user_opt) {
      const auto& old_user = old_user_opt->get();
      if (old_user.email() &&
        (!user.email() || user.email()->string_view() != old_user.email()->string_view())
      ) {
        db_del(txn, db.dbis[User_Email], old_user.email()->string_view());
      }
    }
    if (!old_user_opt || old_user_opt->get().admin() != user.admin()) {
      const auto& s = get_site_stats();
      const auto old_admins = get_admin_list();
      vector admins(old_admins.begin(), old_admins.end());
      auto existing = std::find(admins.begin(), admins.end(), id);
      if (user.admin()) { if (existing == admins.end()) admins.push_back(id); }
      else if (existing != admins.end()) admins.erase(existing);
      db_put(txn, db.dbis[Settings], SettingsKey::admins, string_view{(const char*)admins.data(), admins.size() * sizeof(uint64_t)});
      FlatBufferBuilder fbb;
      fbb.Finish(CreateSiteStats(fbb,
        s.user_count() + (old_user_opt ? 0 : 1),
        s.board_count(),
        s.thread_count(),
        s.comment_count()
      ));
      db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
    }
    db_put(txn, db.dbis[User_Email], user.email()->string_view(), id);
    db_put(txn, db.dbis[LocalUser_User], id, span);
  }
  auto WriteTxn::delete_user(uint64_t id) -> bool {
    const auto user_opt = get_user(id);
    if (!user_opt) {
      spdlog::warn("Tried to delete nonexistent user {:x}", id);
      return false;
    }

    spdlog::debug("Deleting user {:x}", id);
    db_del(txn, db.dbis[User_Name], user_opt->get().name()->string_view());
    db_del(txn, db.dbis[User_User], id);
    db_del(txn, db.dbis[UserStats_User], id);
    db_del(txn, db.dbis[Application_User], id);

    if (const auto local_user_opt = get_local_user(id)) {
      db_del(txn, db.dbis[User_Email], local_user_opt->get().email()->string_view());
      db_del(txn, db.dbis[LocalUser_User], id);
      const auto old_admins = get_admin_list();
      vector admins(old_admins.begin(), old_admins.end());
      auto existing = std::find(admins.begin(), admins.end(), id);
      if (existing != admins.end()) admins.erase(existing);
      db_put(txn, db.dbis[Settings], SettingsKey::admins, string_view{(const char*)admins.data(), admins.size() * sizeof(uint64_t)});
      FlatBufferBuilder fbb;
      const auto& s = get_site_stats();
      fbb.Finish(CreateSiteStats(fbb,
        std::min(s.user_count(), s.user_count() - 1),
        s.board_count(),
        s.thread_count(),
        s.comment_count()
      ));
      db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
    }

    for (const auto board_id : list_subscribed_boards(id)) {
      db_del(txn, db.dbis[UsersSubscribed_Board], board_id, id);
      if (const auto board_stats = get_board_stats(board_id)) {
        const auto& s = board_stats->get();
        FlatBufferBuilder fbb;
        fbb.Finish(CreateBoardStats(fbb,
          s.thread_count(),
          s.comment_count(),
          s.latest_post_time(),
          s.latest_post_id(),
          min(s.subscriber_count(), s.subscriber_count() - 1)
        ));
        db_put(txn, db.dbis[BoardStats_Board], id, fbb.GetBufferSpan());
      }
    }
    db_del(txn, db.dbis[BoardsSubscribed_User], id);
    db_del(txn, db.dbis[InvitesOwned_User], id);
    db_del(txn, db.dbis[ThreadsOwned_User], id);
    db_del(txn, db.dbis[CommentsOwned_User], id);
    db_del(txn, db.dbis[UpvotePost_User], id);
    db_del(txn, db.dbis[DownvotePost_User], id);
    db_del(txn, db.dbis[PostsSaved_User], id);
    db_del(txn, db.dbis[PostsHidden_User], id);
    db_del(txn, db.dbis[UsersHidden_User], id);
    db_del(txn, db.dbis[BoardsHidden_User], id);
    delete_range(txn, db.dbis[ThreadsTop_UserKarma], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[ThreadsNew_UserTime], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[CommentsTop_UserKarma], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[CommentsNew_UserTime], Cursor(id, 0), Cursor(id, ID_MAX));

    // TODO: Delete everything connected to the User
    // TODO: Does this delete owned posts and boards?
    return true;
  }

  auto WriteTxn::create_board(span<uint8_t> span) -> uint64_t {
    const uint64_t id = next_id();
    set_board(id, span);
    return id;
  }
  auto WriteTxn::set_board(uint64_t id, span<uint8_t> span) -> void {
    const auto& board = get_fb<Board>(span);
    if (const auto old_board_opt = get_board(id)) {
      spdlog::debug("Updating board {:x} (name {})", id, board.name()->string_view());
      const auto& old_board = old_board_opt->get();
      if (board.name() != old_board.name()) {
        db_del(txn, db.dbis[Board_Name], old_board.name()->string_view());
      }
    } else {
      spdlog::debug("Creating board {:x} (name {})", id, board.name()->string_view());
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreateBoardStats(fbb));
      db_put(txn, db.dbis[BoardStats_Board], id, fbb.GetBufferSpan());
    }
    db_put(txn, db.dbis[Board_Board], id, span);
    db_put(txn, db.dbis[Board_Name], board.name()->string_view(), id);
    db_put(txn, db.dbis[BoardsNew_Time], board.created_at(), id);
    db_put(txn, db.dbis[BoardsNewPosts_Time], Cursor(0), id);
    db_put(txn, db.dbis[BoardsMostPosts_Posts], Cursor(0), id);
    db_put(txn, db.dbis[BoardsMostSubscribers_Subscribers], Cursor(0), id);
  }
  auto WriteTxn::set_local_board(uint64_t id, span<uint8_t> span) -> void {
    const auto& board = get_fb<LocalBoard>(span);
    assert_fmt(!!get_user(board.owner()), "set_local_board: board {:x} owner user {:x} does not exist", id, board.owner());
    if (const auto old_board_opt = get_local_board(id)) {
      spdlog::debug("Updating local board {:x}", id);
      const auto& old_board = old_board_opt->get();
      if (board.owner() != old_board.owner()) {
        spdlog::info("Changing owner of local board {:x}: {:x} -> {:x}", id, old_board.owner(), board.owner());
        db_del(txn, db.dbis[BoardsOwned_User], old_board.owner(), id);
      }
    } else {
      spdlog::debug("Creating local board {:x}", id);
      FlatBufferBuilder fbb;
      const auto& s = get_site_stats();
      fbb.Finish(CreateSiteStats(fbb,
        s.user_count(),
        s.board_count() + 1,
        s.thread_count(),
        s.comment_count()
      ));
      db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
    }
    db_put(txn, db.dbis[BoardsOwned_User], board.owner(), id);
    db_put(txn, db.dbis[LocalBoard_Board], id, span);
  }
  auto WriteTxn::delete_board(uint64_t id) -> bool {
    const auto board_opt = get_board(id);
    const auto stats_opt = get_board_stats(id);
    if (!board_opt || !stats_opt) {
      spdlog::warn("Tried to delete nonexistent board {:x}", id);
      return false;
    }
    const auto& board = board_opt->get();
    const auto& stats = stats_opt->get();

    spdlog::debug("Deleting board {:x}", id);

    db_del(txn, db.dbis[BoardsNew_Time], board.created_at(), id);
    db_del(txn, db.dbis[BoardsNewPosts_Time], stats.latest_post_time(), id);
    db_del(txn, db.dbis[BoardsMostPosts_Posts], stats.thread_count() + stats.comment_count(), id);
    db_del(txn, db.dbis[BoardsMostSubscribers_Subscribers], stats.subscriber_count(), id);
    db_del(txn, db.dbis[Board_Board], id);
    db_del(txn, db.dbis[BoardStats_Board], id);

    for (const auto user_id : list_subscribers(id)) {
      db_del(txn, db.dbis[BoardsSubscribed_User], user_id, id);
    }
    db_del(txn, db.dbis[UsersSubscribed_Board], id);
    delete_range(txn, db.dbis[ThreadsNew_BoardTime], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[ThreadsTop_BoardKarma], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[CommentsNew_BoardTime], Cursor(id, 0), Cursor(id, ID_MAX));
    delete_range(txn, db.dbis[CommentsTop_BoardKarma], Cursor(id, 0), Cursor(id, ID_MAX));

    if (const auto local_board = get_local_board(id)) {
      spdlog::debug("Deleting local board {:x}", id);
      FlatBufferBuilder fbb;
      const auto& s = get_site_stats();
      fbb.Finish(CreateSiteStats(fbb,
        s.user_count(),
        std::min(s.board_count(), s.board_count() - 1),
        s.thread_count(),
        s.comment_count()
      ));
      db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
      db_del(txn, db.dbis[BoardsOwned_User], local_board->get().owner(), id);
      db_del(txn, db.dbis[LocalBoard_Board], id);
    }

    return true;
  }
  auto WriteTxn::set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void {
    const bool existing = db_has(txn, db.dbis[UsersSubscribed_Board], board_id, user_id);
    const auto board_stats = get_board_stats(board_id);
    auto subscriber_count = board_stats ? board_stats->get().subscriber_count() : 0,
      old_subscriber_count = subscriber_count;
    if (subscribed) {
      assert_fmt(!!get_user(user_id), "set_subscription: user {:x} does not exist", user_id);
      assert_fmt(!!board_stats, "set_subscription: board {:x} does not exist", board_id);
      if (!existing) {
        spdlog::debug("Subscribing user {:x} to board {:x}", user_id, board_id);
        db_put(txn, db.dbis[BoardsSubscribed_User], user_id, board_id);
        db_put(txn, db.dbis[UsersSubscribed_Board], board_id, user_id);
        subscriber_count++;
      }
    } else if (existing) {
      spdlog::debug("Unsubscribing user {:x} from board {:x}", user_id, board_id);
      db_del(txn, db.dbis[BoardsSubscribed_User], user_id, board_id);
      db_del(txn, db.dbis[UsersSubscribed_Board], board_id, user_id);
      subscriber_count = min(subscriber_count, subscriber_count - 1);
    }
    if (board_stats) {
      const auto& s = board_stats->get();
      FlatBufferBuilder fbb;
      fbb.Finish(CreateBoardStats(fbb,
        s.thread_count(),
        s.comment_count(),
        s.latest_post_time(),
        s.latest_post_id(),
        subscriber_count
      ));
      db_put(txn, db.dbis[BoardStats_Board], board_id, fbb.GetBufferSpan());
      db_del(txn, db.dbis[BoardsMostSubscribers_Subscribers], old_subscriber_count, board_id);
      db_put(txn, db.dbis[BoardsMostSubscribers_Subscribers], subscriber_count, board_id);
    }
  }
  auto WriteTxn::set_save(uint64_t user_id, uint64_t post_id, bool saved) -> void {
    assert_fmt(!!get_local_user(user_id), "set_save: local user {:x} does not exist", user_id);
    assert_fmt(!!get_post_stats(post_id), "set_save: post {:x} does not exist", post_id);
    if (saved) db_put(txn, db.dbis[PostsSaved_User], user_id, post_id);
    else db_del(txn, db.dbis[PostsSaved_User], user_id, post_id);
  }
  auto WriteTxn::set_hide_post(uint64_t user_id, uint64_t post_id, bool hidden) -> void {
    assert_fmt(!!get_local_user(user_id), "set_hide_post: local user {:x} does not exist", user_id);
    assert_fmt(!!get_post_stats(post_id), "set_hide_post: post {:x} does not exist", post_id);
    if (hidden) db_put(txn, db.dbis[PostsHidden_User], user_id, post_id);
    else db_del(txn, db.dbis[PostsHidden_User], user_id, post_id);
  }
  auto WriteTxn::set_hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden) -> void {
    assert_fmt(!!get_local_user(user_id), "set_hide_user: local user {:x} does not exist", user_id);
    assert_fmt(!!get_user(hidden_user_id), "set_hide_user: user {:x} does not exist", hidden_user_id);
    if (hidden) db_put(txn, db.dbis[UsersHidden_User], user_id, hidden_user_id);
    else db_del(txn, db.dbis[UsersHidden_User], user_id, hidden_user_id);
  }
  auto WriteTxn::set_hide_board(uint64_t user_id, uint64_t board_id, bool hidden) -> void {
    assert_fmt(!!get_local_user(user_id), "set_hide_board: local user {:x} does not exist", user_id);
    assert_fmt(!!get_board_stats(board_id), "set_hide_board: board {:x} does not exist", board_id);
    if (hidden) db_put(txn, db.dbis[BoardsHidden_User], user_id, board_id);
    else db_del(txn, db.dbis[BoardsHidden_User], user_id, board_id);
  }

  auto WriteTxn::create_thread(span<uint8_t> span) -> uint64_t {
    uint64_t id = next_id();
    set_thread(id, span);
    return id;
  }
  auto WriteTxn::set_thread(uint64_t id, span<uint8_t> span) -> void {
    const auto& thread = get_fb<Thread>(span);
    FlatBufferBuilder fbb;
    const auto author_id = thread.author(), board_id = thread.board(), created_at = thread.created_at();
    if (const auto old_thread_opt = get_thread(id)) {
      spdlog::debug("Updating top-level post {:x} (board {:x}, author {:x})", id, thread.board(), thread.author());
      const auto stats_opt = get_post_stats(id);
      assert_fmt(!!stats_opt, "set_thread: post_stats not in database for existing thread {:x}", id);
      const auto karma = stats_opt->get().karma();
      const auto& old_thread = old_thread_opt->get();
      assert_fmt(author_id == old_thread.author(), "set_thread: cannot change author of thread {:x}", id);
      assert_fmt(created_at == old_thread.created_at(), "set_thread: cannot change created_at of thread {:x}", id);
      const auto old_url = old_thread.content_url() ? Url::parse(old_thread.content_url()->str()) : nullopt,
        new_url = thread.content_url() ? Url::parse(thread.content_url()->str()) : nullopt;
      const auto old_domain = old_url.transform(λx(to_ascii_lowercase(x.host))),
        new_domain = new_url.transform(λx(to_ascii_lowercase(x.host)));
      if (old_domain != new_domain) {
        spdlog::debug("Changing link domain of thread {:x} from {} to {}",
          id, old_domain.value_or("<none>"), new_domain.value_or("<none>")
        );
        if (old_domain && old_url->is_http_s()) db_del(txn, db.dbis[ThreadsByDomain_Domain], *old_domain, id);
        if (new_domain && new_url->is_http_s()) db_put(txn, db.dbis[ThreadsByDomain_Domain], *new_domain, id);
      }
      if (board_id != old_thread.board()) {
        db_del(txn, db.dbis[ThreadsNew_BoardTime], Cursor(old_thread.board(), created_at), id);
        db_del(txn, db.dbis[ThreadsTop_BoardKarma], Cursor(old_thread.board(), karma_uint(karma)), id);
        if (const auto board_stats = get_board_stats(old_thread.board())) {
          const auto& s = board_stats->get();
          fbb.Finish(CreateBoardStats(fbb,
            min(s.thread_count(), s.thread_count() - 1),
            s.comment_count(),
            s.subscriber_count(),
            s.latest_post_time(),
            s.latest_post_id()
          ));
          db_put(txn, db.dbis[BoardStats_Board], old_thread.board(), fbb.GetBufferSpan());
        }
      }
    } else {
      spdlog::debug("Creating top-level post {:x} (board {:x}, author {:x})", id, board_id, author_id);
      db_put(txn, db.dbis[ThreadsNew_Time], created_at, id);
      db_put(txn, db.dbis[ThreadsTop_Karma], karma_uint(0), id);
      db_put(txn, db.dbis[ThreadsMostComments_Comments], Cursor(0), id);
      db_put(txn, db.dbis[ThreadsOwned_User], author_id, id);
      db_put(txn, db.dbis[ThreadsNew_UserTime], Cursor(author_id, created_at), id);
      db_put(txn, db.dbis[ThreadsTop_UserKarma], Cursor(author_id, karma_uint(0)), id);
      db_put(txn, db.dbis[ThreadsNew_BoardTime], Cursor(board_id, created_at), id);
      db_put(txn, db.dbis[ThreadsTop_BoardKarma], Cursor(board_id, karma_uint(0)), id);
      db_put(txn, db.dbis[ThreadsMostComments_BoardComments], Cursor(board_id, 0), id);
      auto url = thread.content_url() ? Url::parse(thread.content_url()->str()) : nullopt;
      if (url && url->is_http_s()) db_put(txn, db.dbis[ThreadsByDomain_Domain], to_ascii_lowercase(url->host), id);
      fbb.ForceDefaults(true);
      fbb.Finish(CreatePostStats(fbb, created_at));
      db_put(txn, db.dbis[PostStats_Post], id, fbb.GetBufferSpan());
      if (!thread.instance()) {
        fbb.Clear();
        const auto& s = get_site_stats();
        fbb.Finish(CreateSiteStats(fbb,
          s.user_count(),
          s.board_count(),
          s.thread_count() + 1,
          s.comment_count()
        ));
        db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
      }
      if (const auto user_stats = get_user_stats(author_id)) {
        const auto& s = user_stats->get();
        const auto last_post_count = s.thread_count() + s.comment_count(),
          last_new_post = s.latest_post_time();
        fbb.Clear();
        fbb.Finish(CreateUserStats(fbb,
          s.thread_count() + 1,
          s.comment_count(),
          s.thread_karma(),
          s.comment_karma(),
          created_at,
          id
        ));
        db_put(txn, db.dbis[UserStats_User], author_id, fbb.GetBufferSpan());
        db_del(txn, db.dbis[UsersNewPosts_Time], last_new_post, author_id);
        db_del(txn, db.dbis[UsersMostPosts_Posts], last_post_count, author_id);
        db_put(txn, db.dbis[UsersNewPosts_Time], created_at, author_id);
        db_put(txn, db.dbis[UsersMostPosts_Posts], last_post_count + 1, author_id);
      }
      if (const auto board_stats = get_board_stats(board_id)) {
        const auto& s = board_stats->get();
        const auto last_post_count = s.thread_count() + s.comment_count(),
          last_new_post = s.latest_post_time();
        fbb.Clear();
        fbb.Finish(CreateBoardStats(fbb,
          s.thread_count() + 1,
          s.comment_count(),
          created_at,
          id,
          s.subscriber_count()
        ));
        db_put(txn, db.dbis[BoardStats_Board], board_id, fbb.GetBufferSpan());
        db_del(txn, db.dbis[BoardsNewPosts_Time], last_new_post, board_id);
        db_del(txn, db.dbis[BoardsMostPosts_Posts], last_post_count, board_id);
        db_put(txn, db.dbis[BoardsNewPosts_Time], created_at, board_id);
        db_put(txn, db.dbis[BoardsMostPosts_Posts], last_post_count + 1, board_id);
      }
    }
    db_put(txn, db.dbis[Thread_Thread], id, span);
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
      const auto last_post_count = s.thread_count() + s.comment_count();
      FlatBufferBuilder fbb;
      fbb.Finish(CreateUserStats(fbb,
        s.thread_count(),
        min(s.comment_count(), s.comment_count() - 1),
        s.thread_karma(),
        karma > 0 ? min(s.comment_karma(), s.comment_karma() - karma) : s.comment_karma() - karma,
        s.latest_post_time(),
        s.latest_post_id()
      ));
      db_put(txn, db.dbis[UserStats_User], comment.author(), fbb.GetBufferSpan());
      db_del(txn, db.dbis[UsersMostPosts_Posts], last_post_count, author);
      db_put(txn, db.dbis[UsersMostPosts_Posts], min(last_post_count, last_post_count - 1), author);
    }
    db_del(txn, db.dbis[CommentsNew_Time], created_at, id);
    db_del(txn, db.dbis[CommentsTop_Karma], karma_uint(karma), id);
    db_del(txn, db.dbis[CommentsMostComments_Comments], descendant_count, id);
    db_del(txn, db.dbis[CommentsOwned_User], author, id);
    db_del(txn, db.dbis[CommentsNew_UserTime], Cursor(author, created_at), id);
    db_del(txn, db.dbis[CommentsTop_UserKarma], Cursor(author, karma_uint(karma)), id);
    db_del(txn, db.dbis[CommentsNew_BoardTime], Cursor(board_id, created_at), id);
    db_del(txn, db.dbis[CommentsTop_BoardKarma], Cursor(board_id, karma_uint(karma)), id);
    db_del(txn, db.dbis[CommentsMostComments_BoardComments], Cursor(board_id, descendant_count), id);

    db_del(txn, db.dbis[ChildrenNew_PostTime], Cursor(parent, created_at), id);
    db_del(txn, db.dbis[ChildrenTop_PostKarma], Cursor(parent, karma_uint(karma)), id);

    std::set<uint64_t> children;
    delete_range(txn, db.dbis[ChildrenNew_PostTime], Cursor(id, 0), Cursor(id, ID_MAX),
      [&children](MDB_val&, MDB_val& v) {
        children.insert(val_as<uint64_t>(v));
      }
    );
    delete_range(txn, db.dbis[ChildrenTop_PostKarma], Cursor(id, 0), Cursor(id, ID_MAX));

    db_del(txn, db.dbis[Comment_Comment], id);
    db_del(txn, db.dbis[PostStats_Post], id);

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
      board_id = thread.board(),
      created_at = thread.created_at(),
      descendant_count = stats.descendant_count();

    spdlog::debug("Deleting top-level post {:x} (board {:x}, author {:x})", id, board_id, author);
    FlatBufferBuilder fbb;
    if (!thread.instance()) {
      const auto& s = get_site_stats();
      fbb.Finish(CreateSiteStats(fbb,
        s.user_count(),
        s.board_count(),
        std::min(s.thread_count(), s.thread_count() - 1),
        s.comment_count()
      ));
      db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
      fbb.Clear();
    }
    if (const auto user_stats = get_user_stats(author)) {
      const auto& s = user_stats->get();
      const auto last_post_count = s.thread_count() + s.comment_count();
      fbb.Finish(CreateUserStats(fbb,
        min(s.thread_count(), s.thread_count() - 1),
        s.comment_count(),
        karma > 0 ? min(s.thread_karma(), s.thread_karma() - karma) : s.thread_karma() - karma,
        s.comment_karma(),
        s.latest_post_time(),
        s.latest_post_id() == id ? 0 : s.latest_post_id()
      ));
      db_put(txn, db.dbis[UserStats_User], author, fbb.GetBufferSpan());
      fbb.Clear();
      db_del(txn, db.dbis[UsersMostPosts_Posts], last_post_count, author);
      db_put(txn, db.dbis[UsersMostPosts_Posts], min(last_post_count, last_post_count - 1), author);
    }
    if (const auto board_stats = get_board_stats(board_id)) {
      const auto& s = board_stats->get();
      const auto last_post_count = s.thread_count() + s.comment_count();
      fbb.Finish(CreateBoardStats(fbb,
        min(s.thread_count(), s.thread_count() - 1),
        min(s.comment_count(), s.comment_count() - descendant_count),
        s.latest_post_time(),
        s.latest_post_id() == id ? 0 : s.latest_post_id(),
        s.subscriber_count()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board_id, fbb.GetBufferSpan());
      fbb.Clear();
      db_del(txn, db.dbis[BoardsMostPosts_Posts], last_post_count, board_id);
      db_put(txn, db.dbis[BoardsMostPosts_Posts], min(last_post_count, last_post_count - descendant_count - 1), board_id);
    }

    // TODO: Delete dangling votes?
    // There used to be a bidirectional User<->Post index for votes,
    // but that almost doubled the size of the database.

    std::set<uint64_t> children;
    delete_range(txn, db.dbis[ChildrenNew_PostTime], Cursor(id, 0), Cursor(id, ID_MAX),
      [&children](MDB_val&, MDB_val& v) {
        children.insert(val_as<uint64_t>(v));
      }
    );
    delete_range(txn, db.dbis[ChildrenTop_PostKarma], Cursor(id, 0), Cursor(id, ID_MAX));

    auto url = thread.content_url() ? Url::parse(thread.content_url()->str()) : nullopt;
    if (url && url->is_http_s()) db_del(txn, db.dbis[ThreadsByDomain_Domain], to_ascii_lowercase(url->host), id);

    db_del(txn, db.dbis[ThreadsNew_Time], created_at, id);
    db_del(txn, db.dbis[ThreadsTop_Karma], karma_uint(karma), id);
    db_del(txn, db.dbis[ThreadsMostComments_Comments], descendant_count, id);
    db_del(txn, db.dbis[ThreadsOwned_User], author);
    db_del(txn, db.dbis[ThreadsNew_UserTime], Cursor(author, created_at), id);
    db_del(txn, db.dbis[ThreadsTop_UserKarma], Cursor(author, karma_uint(karma)), id);
    db_del(txn, db.dbis[ThreadsNew_BoardTime], Cursor(board_id, created_at), id);
    db_del(txn, db.dbis[ThreadsTop_BoardKarma], Cursor(board_id, karma_uint(karma)), id);
    db_del(txn, db.dbis[ThreadsMostComments_BoardComments], Cursor(board_id, descendant_count), id);
    db_del(txn, db.dbis[Thread_Thread], id);
    db_del(txn, db.dbis[PostStats_Post], id);

    for (uint64_t child : children) delete_child_comment(child, board_id);

    return true;
  }

  auto WriteTxn::create_comment(span<uint8_t> span) -> uint64_t {
    uint64_t id = next_id();
    set_comment(id, span);
    return id;
  }
  auto WriteTxn::set_comment(uint64_t id, span<uint8_t> span) -> void {
    using namespace std::chrono;
    const auto& comment = get_fb<Comment>(span);
    const auto stats_opt = get_post_stats(id);
    const auto thread_opt = get_thread(comment.thread());
    assert_fmt(!!thread_opt, "set_comment: comment {:x} top-level ancestor thread {:x} does not exist", id, comment.thread());
    const auto& thread = thread_opt->get();
    const auto author_id = comment.author(), board_id = thread.board(), created_at = comment.created_at();
    const system_clock::time_point created_at_t{seconds(created_at)};
    if (const auto old_comment_opt = get_comment(id)) {
      spdlog::debug("Updating comment {:x} (parent {:x}, author {:x})", id, comment.parent(), comment.author());
      assert(!!stats_opt);
      const auto& old_comment = old_comment_opt->get();
      assert(author_id == old_comment.author());
      assert(comment.parent() == old_comment.parent());
      assert(comment.thread() == old_comment.thread());
      assert(created_at == old_comment.created_at());
    } else {
      spdlog::debug("Creating comment {:x} (parent {:x}, author {:x})", id, comment.parent(), comment.author());
      db_put(txn, db.dbis[CommentsNew_Time], created_at, id);
      db_put(txn, db.dbis[CommentsTop_Karma], karma_uint(0), id);
      db_put(txn, db.dbis[CommentsMostComments_Comments], Cursor(0), id);
      db_put(txn, db.dbis[CommentsOwned_User], author_id, id);
      db_put(txn, db.dbis[CommentsNew_UserTime], Cursor(author_id, created_at), id);
      db_put(txn, db.dbis[CommentsTop_UserKarma], Cursor(author_id, karma_uint(0)), id);
      db_put(txn, db.dbis[CommentsNew_BoardTime], Cursor(board_id, created_at), id);
      db_put(txn, db.dbis[CommentsTop_BoardKarma], Cursor(board_id, karma_uint(0)), id);
      db_put(txn, db.dbis[CommentsMostComments_BoardComments], Cursor(board_id, 0), id);
      db_put(txn, db.dbis[ChildrenNew_PostTime], Cursor(comment.parent(), created_at), id);
      db_put(txn, db.dbis[ChildrenTop_PostKarma], Cursor(comment.parent(), karma_uint(0)), id);
      FlatBufferBuilder fbb;
      fbb.ForceDefaults(true);
      fbb.Finish(CreatePostStats(fbb, created_at));
      db_put(txn, db.dbis[PostStats_Post], id, fbb.GetBufferSpan());

      if (!comment.instance()) {
        fbb.Clear();
        const auto& s = get_site_stats();
        fbb.Finish(CreateSiteStats(fbb,
          s.user_count(),
          s.board_count(),
          s.thread_count(),
          s.comment_count() + 1
        ));
        db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
      }
      for (OptRef<Comment> comment_opt = {comment}; comment_opt; comment_opt = get_comment(comment_opt->get().parent())) {
        const auto parent = comment_opt->get().parent();
        if (const auto parent_stats_opt = get_post_stats(parent)) {
          system_clock::time_point parent_created_at;
          if (const auto parent_opt = get_comment(parent)) {
            parent_created_at = system_clock::time_point(seconds(parent_opt->get().created_at()));
          } else if (parent == comment.thread()) {
            parent_created_at = system_clock::time_point(seconds(thread.created_at()));
          } else continue;
          const auto& s = parent_stats_opt->get();
          const bool is_active = created_at_t >= parent_created_at &&
              created_at_t - parent_created_at <= ACTIVE_COMMENT_MAX_AGE,
            is_newer = is_active && created_at > s.latest_comment();
          const auto last_descendant_count = s.descendant_count();
          fbb.Clear();
          fbb.Finish(CreatePostStats(fbb,
            is_newer ? created_at : s.latest_comment(),
            is_active ? s.latest_comment_necro() : std::max(s.latest_comment_necro(), created_at),
            s.descendant_count() + 1,
            s.child_count() + 1,
            s.upvotes(),
            s.downvotes(),
            s.karma()
          ));
          db_put(txn, db.dbis[PostStats_Post], parent, fbb.GetBufferSpan());
          if (parent == comment.thread()) {
            db_del(txn, db.dbis[ThreadsMostComments_Comments], last_descendant_count, parent);
            db_del(txn, db.dbis[ThreadsMostComments_BoardComments], Cursor(board_id, last_descendant_count), parent);
            db_put(txn, db.dbis[ThreadsMostComments_Comments], last_descendant_count + 1, parent);
            db_put(txn, db.dbis[ThreadsMostComments_BoardComments], Cursor(board_id, last_descendant_count + 1), parent);
          } else {
            db_del(txn, db.dbis[CommentsMostComments_Comments], last_descendant_count, parent);
            db_del(txn, db.dbis[CommentsMostComments_BoardComments], Cursor(board_id, last_descendant_count), parent);
            db_put(txn, db.dbis[CommentsMostComments_Comments], last_descendant_count + 1, parent);
            db_put(txn, db.dbis[CommentsMostComments_BoardComments], Cursor(board_id, last_descendant_count + 1), parent);
          }
        }
      }
      if (const auto user_stats = get_user_stats(author_id)) {
        const auto& s = user_stats->get();
        const auto last_post_count = s.thread_count() + s.comment_count(),
          last_new_post = s.latest_post_time();
        fbb.Clear();
        fbb.Finish(CreateUserStats(fbb,
          s.thread_count(),
          s.comment_count() + 1,
          s.thread_karma(),
          s.comment_karma(),
          created_at,
          id
        ));
        db_put(txn, db.dbis[UserStats_User], author_id, fbb.GetBufferSpan());
        db_del(txn, db.dbis[UsersNewPosts_Time], last_new_post, author_id);
        db_del(txn, db.dbis[UsersMostPosts_Posts], last_post_count, author_id);
        db_put(txn, db.dbis[UsersNewPosts_Time], created_at, author_id);
        db_put(txn, db.dbis[UsersMostPosts_Posts], last_post_count + 1, author_id);
      }
      if (const auto board_stats = get_board_stats(board_id)) {
        const auto& s = board_stats->get();
        const auto last_post_count = s.thread_count() + s.comment_count(),
          last_new_post = s.latest_post_time();
        fbb.Clear();
        fbb.Finish(CreateBoardStats(fbb,
          s.thread_count(),
          s.comment_count() + 1,
          created_at,
          id,
          s.subscriber_count()
        ));
        db_put(txn, db.dbis[BoardStats_Board], board_id, fbb.GetBufferSpan());
        db_del(txn, db.dbis[BoardsNewPosts_Time], last_new_post, board_id);
        db_del(txn, db.dbis[BoardsMostPosts_Posts], last_post_count, board_id);
        db_put(txn, db.dbis[BoardsNewPosts_Time], created_at, board_id);
        db_put(txn, db.dbis[BoardsMostPosts_Posts], last_post_count + 1, board_id);
      }
    }
    db_put(txn, db.dbis[Comment_Comment], id, span);
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
    if (!comment.instance()) {
      const auto& s = get_site_stats();
      const auto next_comment_count =
        (descendant_count + 1) > s.comment_count() ? 0 : s.comment_count() - (descendant_count + 1);
      fbb.Finish(CreateSiteStats(fbb,
        s.user_count(),
        s.board_count(),
        s.thread_count(),
        next_comment_count
      ));
      db_put(txn, db.dbis[Settings], SettingsKey::site_stats, fbb.GetBufferSpan());
      fbb.Clear();
    }
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
        db_put(txn, db.dbis[PostStats_Post], parent, fbb.GetBufferSpan());
        if (parent == comment.thread()) {
          db_del(txn, db.dbis[ThreadsMostComments_Comments], last_descendant_count, parent);
          db_del(txn, db.dbis[ThreadsMostComments_BoardComments], Cursor(board_id, last_descendant_count), parent);
          db_put(txn, db.dbis[ThreadsMostComments_Comments], next_descendant_count, parent);
          db_put(txn, db.dbis[ThreadsMostComments_BoardComments], Cursor(board_id, next_descendant_count), parent);
        } else {
          db_del(txn, db.dbis[CommentsMostComments_Comments], last_descendant_count, parent);
          db_del(txn, db.dbis[CommentsMostComments_BoardComments], Cursor(board_id, last_descendant_count), parent);
          db_put(txn, db.dbis[CommentsMostComments_Comments], next_descendant_count, parent);
          db_put(txn, db.dbis[CommentsMostComments_BoardComments], Cursor(board_id, next_descendant_count), parent);
        }
      }
    }
    if (const auto board_stats_opt = get_board_stats(board_id)) {
      const auto& s = board_stats_opt->get();
      const auto last_post_count = s.thread_count() + s.comment_count();
      fbb.Clear();
      fbb.Finish(CreateBoardStats(fbb,
        s.thread_count(),
        (descendant_count + 1) > s.comment_count() ? 0 : s.comment_count() - (descendant_count + 1),
        s.latest_post_time(),
        s.latest_post_id() == id ? 0 : s.latest_post_id(),
        s.subscriber_count()
      ));
      db_put(txn, db.dbis[BoardStats_Board], board_id, fbb.GetBufferSpan());
      db_del(txn, db.dbis[BoardsMostPosts_Posts], last_post_count, board_id);
      db_put(txn, db.dbis[BoardsMostPosts_Posts], min(last_post_count, last_post_count - descendant_count - 1), board_id);
    }

    return delete_child_comment(id, board_id);
  }

  auto WriteTxn::set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void {
    const auto existing = static_cast<int64_t>(get_vote_of_user_for_post(user_id, post_id));
    const int64_t diff = static_cast<int64_t>(vote) - existing;
    if (!diff) return;
    const auto thread_opt = get_thread(post_id);
    const auto comment_opt = thread_opt ? nullopt : get_comment(post_id);
    if (!thread_opt && !comment_opt) {
      throw DBError(fmt::format("Cannot set vote on post {:x}", post_id), MDB_NOTFOUND);
    }
    const auto op_id = thread_opt ? thread_opt->get().author() : comment_opt->get().author();
    spdlog::debug("Setting vote from user {:x} on post {:x} to {}", user_id, post_id, (int8_t)vote);
    switch (vote) {
      case Vote::Upvote:
        db_put(txn, db.dbis[UpvotePost_User], user_id, post_id);
        db_del(txn, db.dbis[DownvotePost_User], user_id, post_id);
        break;
      case Vote::NoVote:
        db_del(txn, db.dbis[UpvotePost_User], user_id, post_id);
        db_del(txn, db.dbis[DownvotePost_User], user_id, post_id);
        break;
      case Vote::Downvote:
        db_del(txn, db.dbis[UpvotePost_User], user_id, post_id);
        db_put(txn, db.dbis[DownvotePost_User], user_id, post_id);
        break;
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
        vote > Vote::NoVote ? s.upvotes() + 1 : (existing > 0 ? min(s.upvotes(), s.upvotes() - 1) : s.upvotes()),
        vote < Vote::NoVote ? s.downvotes() + 1 : (existing < 0 ? min(s.downvotes(), s.downvotes() - 1) : s.downvotes()),
        new_karma
      ));
      db_put(txn, db.dbis[PostStats_Post], post_id, fbb.GetBufferSpan());
    }
    if (const auto op_stats_opt = get_user_stats(op_id)) {
      const auto& s = op_stats_opt->get();
      fbb.Clear();
      fbb.Finish(CreateUserStats(fbb,
        s.thread_count(),
        s.comment_count(),
        s.thread_karma() + (thread_opt ? diff : 0),
        s.comment_karma() + (thread_opt ? 0 : diff),
        s.latest_post_time(),
        s.latest_post_id()
      ));
      db_put(txn, db.dbis[UserStats_User], op_id, fbb.GetBufferSpan());
    }
    if (thread_opt) {
      const auto& thread = thread_opt->get();
      db_del(txn, db.dbis[ThreadsTop_Karma], karma_uint(old_karma), post_id);
      db_del(txn, db.dbis[ThreadsTop_BoardKarma], Cursor(thread.board(), karma_uint(old_karma)), post_id);
      db_del(txn, db.dbis[ThreadsTop_UserKarma], Cursor(thread.author(), karma_uint(old_karma)), post_id);
      db_put(txn, db.dbis[ThreadsTop_Karma], karma_uint(new_karma), post_id);
      db_put(txn, db.dbis[ThreadsTop_BoardKarma], Cursor(thread.board(), karma_uint(new_karma)), post_id);
      db_put(txn, db.dbis[ThreadsTop_UserKarma], Cursor(thread.author(), karma_uint(new_karma)), post_id);
    } else {
      const auto& comment = comment_opt->get();
      db_del(txn, db.dbis[CommentsTop_Karma], karma_uint(old_karma), post_id);
      db_del(txn, db.dbis[CommentsTop_UserKarma], Cursor(comment.author(), karma_uint(old_karma)), post_id);
      db_del(txn, db.dbis[ChildrenTop_PostKarma], Cursor(comment.parent(), karma_uint(old_karma)), post_id);
      db_put(txn, db.dbis[CommentsTop_Karma], karma_uint(new_karma), post_id);
      db_put(txn, db.dbis[CommentsTop_UserKarma], Cursor(comment.author(), karma_uint(new_karma)), post_id);
      db_put(txn, db.dbis[ChildrenTop_PostKarma], Cursor(comment.parent(), karma_uint(new_karma)), post_id);
      if (const auto comment_thread_opt = get_thread(comment.thread())) {
        const auto& comment_thread = comment_thread_opt->get();
        db_del(txn, db.dbis[CommentsTop_BoardKarma], Cursor(comment_thread.board(), karma_uint(old_karma)), post_id);
        db_put(txn, db.dbis[CommentsTop_BoardKarma], Cursor(comment_thread.board(), karma_uint(new_karma)), post_id);
      }
    }
  }

  auto WriteTxn::create_application(uint64_t user_id, span<uint8_t> span) -> void {
    assert_fmt(!!get_local_user(user_id), "create_application: local user {:x} does not exist", user_id);
    db_put(txn, db.dbis[Application_User], user_id, span);
  }
  auto WriteTxn::create_invite(uint64_t sender_user_id, uint64_t lifetime_seconds) -> uint64_t {
    uint64_t id, now = now_s();
    duthomhas::csprng()(id);
    FlatBufferBuilder fbb;
    fbb.Finish(CreateInvite(fbb, now, now + lifetime_seconds, sender_user_id));
    set_invite(id, fbb.GetBufferSpan());
    return id;
  }
  auto WriteTxn::set_invite(uint64_t invite_id, span<uint8_t> span) -> void {
    const auto& invite = get_fb<Invite>(span);
    if (const auto old_invite = get_invite(invite_id)) {
      assert_fmt(invite.created_at() == old_invite->get().created_at(), "set_invite: cannot change created_at field of invite");
      assert_fmt(invite.from() == old_invite->get().from(), "set_invite: cannot change from field of invite");
    } else {
      assert_fmt(!!get_local_user(invite.from()), "set_invite: local user {:x} does not exist", invite.from());
      db_put(txn, db.dbis[InvitesOwned_User], invite.from(), invite_id);
    }
    db_put(txn, db.dbis[Invite_Invite], invite_id, span);
  }
  auto WriteTxn::delete_invite(uint64_t invite_id) -> void {
    if (auto invite = get_invite(invite_id)) {
      db_del(txn, db.dbis[InvitesOwned_User], invite->get().from(), invite_id);
    }
    db_del(txn, db.dbis[Invite_Invite], invite_id);
  }

  auto WriteTxn::set_link_card(string_view url, span<uint8_t> span) -> void {
    get_fb<LinkCard>(span);
    MDB_val vval{ span.size(), span.data() };
    db_put(txn, db.dbis[LinkCard_Url], url, vval);
  }
  auto WriteTxn::delete_link_card(string_view url) -> void {
    db_del(txn, db.dbis[LinkCard_Url], url);
  }
}
