#pragma once
#include "util/common.h++"
#include "util/iter.h++"
#include "util/jwt.h++"
#include "services/search_engine.h++"
#include "models/db.h++"
#include <atomic>
#include <openssl/evp.h>

namespace Ludwig {
  static inline auto karma_uint(int64_t karma) -> uint64_t {
    if (karma < 0) return (uint64_t)(std::numeric_limits<int64_t>::max() + karma);
    return (uint64_t)std::numeric_limits<int64_t>::max() + (uint64_t)karma;
  }

  static constexpr auto ACTIVE_COMMENT_MAX_AGE = std::chrono::hours(48);

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

  namespace SettingsKey {
    inline static constexpr std::string_view
      // Not exported
      site_stats {"site_stats"},
      admins {"admins"},

      // Exported
      next_id {"next_id"},
      setup_done {"setup_done"},
      jwt_secret {"jwt_secret"},
      private_key {"private_key"},
      public_key {"public_key"},
      base_url {"base_url"},
      created_at {"created_at"},
      updated_at {"updated_at"},
      name {"name"},
      description {"description"},
      icon_url {"icon_url"},
      banner_url {"banner_url"},
      post_max_length {"post_max_length"},
      home_page_type {"home_page_type"},
      media_upload_enabled {"media_upload_enabled"},
      image_max_bytes {"image_max_bytes"},
      video_max_bytes {"video_max_bytes"},
      javascript_enabled {"javascript_enabled"},
      infinite_scroll_enabled {"infinite_scroll_enabled"},
      board_creation_admin_only {"board_creation_admin_only"},
      registration_enabled {"registration_enabled"},
      registration_application_required {"registration_application_required"},
      registration_invite_required {"registration_invite_required"},
      invite_admin_only {"invite_admin_only"},
      federation_enabled {"federation_enabled"},
      federate_cw_content {"federate_cw_content"},
      application_question {"application_question"},
      votes_enabled {"votes_enabled"},
      downvotes_enabled {"downvotes_enabled"},
      cws_enabled {"cws_enabled"},
      require_login_to_view {"require_login_to_view"},
      default_board_id {"default_board_id"},
      color_accent {"color_accent"},
      color_accent_dim {"color_accent_dim"},
      color_accent_hover {"color_accent_hover"};

    static inline auto is_exported(std::string_view key) -> bool {
      return key != site_stats && key != admins;
    }
  };

  class ReadTxnBase;
  class ReadTxnImpl;
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
    std::atomic<uint8_t> session_counter;
    auto init_env(const char* filename, MDB_txn** txn) -> int;
  public:
    DB(const char* filename, size_t map_size_mb = 1024);
    DB(
      const char* filename,
      std::function<size_t (uint8_t*, size_t)> read,
      std::optional<std::shared_ptr<SearchEngine>> search = {},
      size_t map_size_mb = 1024
    );
    DB(const DB&) = delete;
    auto operator=(const DB&) = delete;
    DB(DB&&);
    auto operator=(DB&&) -> DB&;
    ~DB();

    auto open_read_txn() -> ReadTxnImpl;
    auto open_write_txn() -> WriteTxn;

    friend class ReadTxnBase;
    friend class ReadTxnImpl;
    friend class WriteTxn;
  };

  class ReadTxnBase {
  protected:
    DB& db;
    MDB_txn* txn;
    ReadTxnBase(DB& db) : db(db) {}
  public:
    ReadTxnBase(const ReadTxnBase& from) = delete;
    auto operator=(const ReadTxnBase&) = delete;
    ReadTxnBase(ReadTxnBase&& from) : db(from.db), txn(from.txn) { from.txn = nullptr; }
    ReadTxnBase& operator=(ReadTxnBase&& from) = delete;
    virtual ~ReadTxnBase() = default;

    using OptCursor = const std::optional<Cursor>&;
    using OptKV = const std::optional<std::pair<Cursor, uint64_t>>&;

    auto get_setting_str(std::string_view key) -> std::string_view;
    auto get_setting_int(std::string_view key) -> uint64_t;
    auto get_jwt_secret() -> JwtSecret;
    auto get_public_key() -> std::unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)>;
    auto get_private_key() -> std::unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)>;
    auto get_site_stats() -> const SiteStats&;
    auto get_admin_list() -> std::span<uint64_t>;
    auto get_session(uint64_t session_id) -> OptRef<const Session>;

    auto get_user_id_by_name(std::string_view name) -> std::optional<uint64_t>;
    auto get_user_id_by_email(std::string_view email) -> std::optional<uint64_t>;
    auto get_user(uint64_t id) -> OptRef<const User>;
    auto get_user_stats(uint64_t id) -> OptRef<const UserStats>;
    auto get_local_user(uint64_t id) -> OptRef<const LocalUser>;
    auto count_local_users() -> uint64_t;
    auto list_users_alphabetical(std::optional<std::string_view> cursor = {}) -> DBIter;
    auto list_users_new(OptKV cursor = {}) -> DBIter;
    auto list_users_old(OptKV cursor = {}) -> DBIter;
    auto list_users_new_posts(OptKV cursor = {}) -> DBIter;
    auto list_users_most_posts(OptKV cursor = {}) -> DBIter;
    auto list_subscribers(uint64_t board_id, OptCursor cursor = {}) -> DBIter;
    auto is_user_subscribed_to_board(uint64_t user_id, uint64_t board_id) -> bool;

    auto get_board_id_by_name(std::string_view name) -> std::optional<uint64_t>;
    auto get_board(uint64_t id) -> OptRef<const Board>;
    auto get_board_stats(uint64_t id) -> OptRef<const BoardStats>;
    auto get_local_board(uint64_t name) -> OptRef<const LocalBoard>;
    auto count_local_boards() -> uint64_t;
    auto list_boards_alphabetical(std::optional<std::string_view> cursor = {}) -> DBIter;
    auto list_boards_new(OptKV cursor = {}) -> DBIter;
    auto list_boards_old(OptKV cursor = {}) -> DBIter;
    auto list_boards_new_posts(OptKV cursor = {}) -> DBIter;
    auto list_boards_most_posts(OptKV cursor = {}) -> DBIter;
    auto list_boards_most_subscribers(OptKV cursor = {}) -> DBIter;
    auto list_subscribed_boards(uint64_t user_id, OptCursor cursor = {}) -> DBIter;
    auto list_created_boards(uint64_t user_id, OptCursor cursor = {}) -> DBIter;

    auto get_thread(uint64_t id) -> OptRef<const Thread>;
    auto get_post_stats(uint64_t id) -> OptRef<const PostStats>;
    auto list_threads_new(OptKV cursor = {}) -> DBIter;
    auto list_threads_old(OptKV cursor = {}) -> DBIter;
    auto list_threads_top(OptKV cursor = {}) -> DBIter;
    auto list_threads_most_comments(OptKV cursor = {}) -> DBIter;
    auto list_threads_of_board_new(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_threads_of_board_old(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_threads_of_board_top(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_threads_of_board_most_comments(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_threads_of_user_new(uint64_t user_id, OptKV cursor = {}) -> DBIter;
    auto list_threads_of_user_old(uint64_t user_id, OptKV cursor = {}) -> DBIter;
    auto list_threads_of_user_top(uint64_t user_id, OptKV cursor = {}) -> DBIter;

    auto get_comment(uint64_t id) -> OptRef<Comment>;
    auto list_comments_new(OptKV cursor = {}) -> DBIter;
    auto list_comments_old(OptKV cursor = {}) -> DBIter;
    auto list_comments_top(OptKV cursor = {}) -> DBIter;
    auto list_comments_most_comments(OptKV cursor = {}) -> DBIter;
    auto list_comments_of_post_new(uint64_t post_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_post_old(uint64_t post_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_post_top(uint64_t post_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_board_new(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_board_old(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_board_top(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_board_most_comments(uint64_t board_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_user_new(uint64_t user_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_user_old(uint64_t user_id, OptKV cursor = {}) -> DBIter;
    auto list_comments_of_user_top(uint64_t user_id, OptKV cursor = {}) -> DBIter;

    auto get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote;
    auto list_upvoted_posts_of_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter;
    auto list_downvoted_posts_of_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter;
    auto list_saved_posts_of_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter;

    auto has_user_saved_post(uint64_t user_id, uint64_t post_id) -> bool;
    auto has_user_hidden_post(uint64_t user_id, uint64_t post_id) -> bool;
    auto has_user_hidden_user(uint64_t user_id, uint64_t hidden_user_id) -> bool;
    auto has_user_hidden_board(uint64_t user_id, uint64_t board_id) -> bool;

    auto get_application(uint64_t user_id) -> OptRef<const Application>;
    auto list_applications(OptCursor cursor = {}) -> DBIter;

    auto get_invite(uint64_t invite_id) -> OptRef<const Invite>;
    auto list_invites_from_user(uint64_t user_id, OptCursor cursor = {}) -> DBIter;

    auto get_link_card(std::string_view url) -> OptRef<const LinkCard>;
    auto list_threads_of_domain(std::string_view domain, OptKV cursor = {}) -> DBIter;

    // TODO: DMs, Blocks, Admins/Mods, Mod Actions

    auto dump(uWS::MoveOnlyFunction<void (const flatbuffers::span<uint8_t>&, bool)> on_data) -> void;

    friend class ReadTxnImpl;
  };

  class ReadTxnImpl : public ReadTxnBase {
  protected:
    ReadTxnImpl(DB& db) : ReadTxnBase(db) {
      if (auto err = mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn)) {
        throw DBError("Failed to open read transaction", err);
      }
    }
  public:
    ReadTxnImpl(ReadTxnImpl&& from) : ReadTxnBase(std::move(from)) {};
    ~ReadTxnImpl() {
      if (txn != nullptr) mdb_txn_abort(txn);
    }

    friend class DB;
  };

  class WriteTxn : public ReadTxnBase {
  protected:
    bool committed = false;
    auto delete_child_comment(uint64_t id, uint64_t board_id) -> uint64_t;

    WriteTxn(DB& db): ReadTxnBase(db) {
      if (auto err = mdb_txn_begin(db.env, nullptr, 0, &txn)) {
        throw DBError("Failed to open write transaction", err);
      }
    };
  public:
    WriteTxn(WriteTxn&& from) : ReadTxnBase(std::move(from)), committed(from.committed) {}
    ~WriteTxn() {
      if (!committed) {
        spdlog::warn("Aborting uncommitted write transaction");
        if (txn != nullptr) mdb_txn_abort(txn);
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

    auto create_user(flatbuffers::span<uint8_t> span) -> uint64_t;
    auto set_user(uint64_t id, flatbuffers::span<uint8_t> span) -> void;
    auto set_local_user(uint64_t id, flatbuffers::span<uint8_t> span) -> void;
    auto delete_user(uint64_t id) -> bool;

    auto create_board(flatbuffers::span<uint8_t> span) -> uint64_t;
    auto set_board(uint64_t id, flatbuffers::span<uint8_t> span) -> void;
    auto set_local_board(uint64_t id, flatbuffers::span<uint8_t> span) -> void;
    auto delete_board(uint64_t id) -> bool;
    auto set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void;

    auto create_thread(flatbuffers::span<uint8_t> span) -> uint64_t;
    auto set_thread(uint64_t id, flatbuffers::span<uint8_t> span) -> void;
    auto delete_thread(uint64_t id) -> bool;

    auto create_comment(flatbuffers::span<uint8_t> span) -> uint64_t;
    auto set_comment(uint64_t id, flatbuffers::span<uint8_t> span) -> void;
    auto delete_comment(uint64_t id) -> uint64_t;

    auto set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void;
    auto set_save(uint64_t user_id, uint64_t post_id, bool saved) -> void;
    auto set_hide_post(uint64_t user_id, uint64_t post_id, bool hidden) -> void;
    auto set_hide_user(uint64_t user_id, uint64_t hidden_user_id, bool hidden) -> void;
    auto set_hide_board(uint64_t user_id, uint64_t board_id, bool hidden) -> void;

    auto create_application(uint64_t user_id, flatbuffers::span<uint8_t> span) -> void;
    auto create_invite(uint64_t sender_user_id, uint64_t lifetime_seconds) -> uint64_t;
    auto set_invite(uint64_t invite_id, flatbuffers::span<uint8_t> span) -> void;
    auto delete_invite(uint64_t invite_id) -> void;

    auto set_link_card(std::string_view url, flatbuffers::span<uint8_t> span) -> void;
    auto delete_link_card(std::string_view url) -> void;

    inline auto commit() -> void {
      auto err = mdb_txn_commit(txn);
      if (err) throw DBError("Failed to commit transaction", err);
      committed = true;
    }

    friend class DB;
  };

  inline auto DB::open_read_txn() -> ReadTxnImpl {
    return ReadTxnImpl(*this);
  }

  inline auto DB::open_write_txn() -> WriteTxn {
    return WriteTxn(*this);
  }
}
