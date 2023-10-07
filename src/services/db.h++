#pragma once
#include "util/iter.h++"
#include "util/jwt.h++"
#include "models/db.h++"
#include <atomic>

namespace Ludwig {
  static inline auto karma_uint(int64_t karma) -> uint64_t {
    if (karma < 0) return (uint64_t)(std::numeric_limits<int64_t>::max() + karma);
    return (uint64_t)std::numeric_limits<int64_t>::max() + (uint64_t)karma;
  }

  class StringVal {
  private:
    const std::string str;
  public:
    const MDB_val val;
    StringVal(std::string str) : str(str), val{str.length(), str.data()} {}
    inline operator const MDB_val*() const {
      return &val;
    }
  };

  class SettingsKey {
  public:
    inline static constexpr std::string_view
      next_id {"next_id"},
      hash_seed {"hash_seed"},
      jwt_secret {"jwt_secret"},
      private_key {"private_key"},
      public_key {"public_key"},
      domain {"domain"},
      created_at {"created_at"},
      updated_at {"updated_at"},
      name {"name"},
      description {"description"},
      icon_url {"icon_url"},
      banner_url {"banner_url"},
      post_max_length {"post_max_length"},
      media_upload_enabled {"media_upload_enabled"},
      image_max_bytes {"image_max_bytes"},
      video_max_bytes {"video_max_bytes"},
      javascript_enabled {"javascript_enabled"},
      board_creation_admin_only {"board_creation_admin_only"},
      registration_enabled {"registration_enabled"},
      registration_application_required {"registration_application_required"},
      registration_invite_required {"registration_invite_required"},
      invite_admin_only {"invite_admin_only"},
      federation_enabled {"federation_enabled"},
      federate_cw_content {"federate_cw_content"};
  };

  class ReadTxnBase;
  class ReadTxn;
  class WriteTxn;

  class DBError : public std::runtime_error {
  public:
    DBError(std::string message, int mdb_error) :
      std::runtime_error(message + ": " + std::string(mdb_strerror(mdb_error))) {}
  };

  class DB {
  private:
    size_t map_size;
    MDB_env* env;
    MDB_dbi dbis[128];
    uint8_t session_counter;
    auto init_env(const char* filename, MDB_txn** txn) -> int;
  public:
    uint64_t seed;
    uint8_t jwt_secret[JWT_SECRET_SIZE];
    DB(const char* filename, size_t map_size_mb = 1024);
    DB(const char* filename, std::istream& dump_stream, size_t map_size_mb = 1024);
    ~DB();

    auto open_read_txn() -> ReadTxn;
    auto open_write_txn() -> WriteTxn;

    friend class ReadTxnBase;
    friend class ReadTxn;
    friend class WriteTxn;
  };

  template <typename T> using OptRef = std::optional<std::reference_wrapper<const T>>;

  class ReadTxnBase {
  protected:
    DB& db;
    MDB_txn* txn;
    ReadTxnBase(DB& db) : db(db) {}
  public:
    virtual ~ReadTxnBase() {};

    ReadTxnBase (const ReadTxnBase&) = delete;
    ReadTxnBase& operator= (const ReadTxnBase&) = delete;

    using OptCursor = const std::optional<Cursor>&;

    auto get_setting_str(std::string_view key) -> std::string_view;
    auto get_setting_int(std::string_view key) -> uint64_t;

    auto get_session(uint64_t session_id) -> OptRef<Session>;

    auto get_user_id_by_name(std::string_view name) -> std::optional<uint64_t>;
    auto get_user_id_by_email(std::string_view email) -> std::optional<uint64_t>;
    auto get_user(uint64_t id) -> OptRef<User>;
    auto get_user_stats(uint64_t id) -> OptRef<UserStats>;
    auto get_local_user(uint64_t id) -> OptRef<LocalUser>;
    auto count_local_users() -> uint64_t;
    auto list_users(OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_local_users(OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_subscribers(uint64_t board_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto is_user_subscribed_to_board(uint64_t user_id, uint64_t board_id) -> bool;

    auto get_board_id_by_name(std::string_view name) -> std::optional<uint64_t>;
    auto get_board(uint64_t id) -> OptRef<Board>;
    auto get_board_stats(uint64_t id) -> OptRef<BoardStats>;
    auto get_local_board(uint64_t name) -> OptRef<LocalBoard>;
    auto count_local_boards() -> uint64_t;
    auto list_boards(OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_local_boards(OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_subscribed_boards(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_created_boards(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;

    auto get_thread(uint64_t id) -> OptRef<Thread>;
    auto get_post_stats(uint64_t id) -> OptRef<PostStats>;
    auto list_threads_of_board_new(uint64_t board_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_threads_of_board_top(uint64_t board_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_threads_of_user_new(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_threads_of_user_top(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;

    auto get_comment(uint64_t id) -> OptRef<Comment>;
    auto list_comments_of_post_new(uint64_t post_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_comments_of_post_top(uint64_t post_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_comments_of_board_new(uint64_t board_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_comments_of_board_top(uint64_t board_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_comments_of_user_new(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    auto list_comments_of_user_top(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;

    auto get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote;
    //auto list_upvoted_posts_of_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    //auto list_downvoted_posts_of_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;
    //auto list_saved_posts_of_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;

    auto has_user_saved_post(uint64_t user_id, uint64_t post_id) -> bool;
    auto has_user_hidden_post(uint64_t user_id, uint64_t post_id) -> bool;
    auto has_user_hidden_user(uint64_t user_id, uint64_t hidden_user_id) -> bool;
    auto has_user_hidden_board(uint64_t user_id, uint64_t board_id) -> bool;

    auto get_application(uint64_t user_id) -> OptRef<Application>;
    auto list_applications(OptCursor cursor = {}) -> DBIter<uint64_t>;

    auto get_invite(uint64_t invite_id) -> OptRef<Invite>;
    auto list_invites_from_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter<uint64_t>;

    // TODO: Feeds, DMs, Blocks, Admins/Mods, Mod Actions

    friend class ReadTxn;
  };

  class ReadTxn : public ReadTxnBase {
  protected:
    ReadTxn(DB& db) : ReadTxnBase(db) {
      if (auto err = mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn)) {
        throw DBError("Failed to open read transaction", err);
      }
    }
  public:
    ~ReadTxn() {
      mdb_txn_abort(txn);
    }

    friend class DB;
  };

  class WriteTxn : public ReadTxnBase {
  protected:
    bool committed = false;
    std::atomic<uint8_t> session_counter;
    auto delete_child_comment(uint64_t id, uint64_t board_id) -> uint64_t;

    WriteTxn(DB& db): ReadTxnBase(db) {
      if (auto err = mdb_txn_begin(db.env, nullptr, 0, &txn)) {
        throw DBError("Failed to open write transaction", err);
      }
    };
  public:
    ~WriteTxn() {
      if (!committed) {
        spdlog::warn("Aborting uncommitted write transaction");
        mdb_txn_abort(txn);
      }
    }

    auto next_id() -> uint64_t;
    auto set_setting(std::string_view key, std::string_view value) -> void;
    auto set_setting(std::string_view key, uint64_t value) -> void;

    auto create_session(
      uint64_t user,
      std::string_view ip,
      std::string_view user_agent,
      bool remember = false,
      uint64_t lifetime_seconds = 15 * 60
    ) -> std::pair<uint64_t, uint64_t>;
    auto delete_session(uint64_t session) -> void;

    auto create_user(flatbuffers::FlatBufferBuilder& builder) -> uint64_t;
    auto set_user(uint64_t id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto set_local_user(uint64_t id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto delete_user(uint64_t id) -> bool;

    auto create_board(flatbuffers::FlatBufferBuilder& builder) -> uint64_t;
    auto set_board(uint64_t id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto set_local_board(uint64_t id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto delete_board(uint64_t id) -> bool;
    auto set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void;

    auto create_thread(flatbuffers::FlatBufferBuilder& builder) -> uint64_t;
    auto set_thread(uint64_t id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto delete_thread(uint64_t id) -> bool;

    auto create_comment(flatbuffers::FlatBufferBuilder& builder) -> uint64_t;
    auto set_comment(uint64_t id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto delete_comment(uint64_t id) -> uint64_t;

    auto set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void;
    auto set_save(uint64_t user_id, uint64_t post_id, bool saved) -> void;
    auto set_hide_post(uint64_t user_id, uint64_t post_id, bool hidden) -> void;
    auto set_hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden) -> void;
    auto set_hide_board(uint64_t user_id, uint64_t board_id, bool hidden) -> void;

    auto create_application(uint64_t user_id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto create_invite(uint64_t sender_user_id, uint64_t lifetime_seconds) -> uint64_t;
    auto set_invite(uint64_t invite_id, flatbuffers::FlatBufferBuilder& builder) -> void;
    auto delete_invite(uint64_t invite_id) -> void;

    inline auto commit() -> void {
      auto err = mdb_txn_commit(txn);
      if (err) throw DBError("Failed to commit transaction", err);
      committed = true;
    }

    friend class DB;
  };

  inline auto DB::open_read_txn() -> ReadTxn {
    return ReadTxn(*this);
  }

  inline auto DB::open_write_txn() -> WriteTxn {
    return WriteTxn(*this);
  }
}
