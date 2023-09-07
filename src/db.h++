#pragma once
#include "iter.h++"
#include "jwt.h++"
#include "generated/datatypes_generated.h"

namespace Ludwig {
  enum Vote {
    Downvote = -1,
    NoVote = 0,
    Upvote = 1
  };

  class SettingsKey {
  public:
    inline static const MDBInVal
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
      board_creation_admin_only {"board_creation_admin_only"},
      federation_enabled {"federation_enabled"};
  };

  class ReadTxnBase;
  class ReadTxn;
  class WriteTxn;

  class DB {
  private:
    MDBEnv env;
    MDBDbi dbis[128];
  public:
    uint64_t seed;
    uint8_t jwt_secret[JWT_SECRET_SIZE];
    DB(const char* filename);
    auto open_read_txn() -> ReadTxn;
    auto open_write_txn() -> WriteTxn;
    friend class ReadTxnBase;
    friend class ReadTxn;
    friend class WriteTxn;
  };

  class ReadTxnBase {
  protected:
    DB& db;
    virtual auto ro_txn() -> MDBROTransactionImpl& = 0;
  public:
    ReadTxnBase(DB& db) : db(db) {}

    auto get_setting_str(MDBInVal key) -> std::string_view;
    auto get_setting_int(MDBInVal key) -> uint64_t;

    auto get_user_id(std::string_view name) -> std::optional<uint64_t>;
    auto get_user(uint64_t id) -> std::optional<const User*>;
    auto get_user_stats(uint64_t id) -> std::optional<const UserStats*>;
    auto get_local_user(uint64_t id) -> std::optional<const LocalUser*>;
    auto count_local_users() -> uint64_t;
    auto list_users(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_local_users(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_subscribers(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto user_is_subscribed(uint64_t user_id, uint64_t board_id) -> bool;

    auto get_board_id(std::string_view name) -> std::optional<uint64_t>;
    auto get_board(uint64_t id) -> std::optional<const Board*>;
    auto get_board_stats(uint64_t id) -> std::optional<const BoardStats*>;
    auto get_local_board(uint64_t name) -> std::optional<const LocalBoard*>;
    auto count_local_boards() -> uint64_t;
    auto list_boards(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_local_boards(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_subscribed_boards(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_created_boards(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;

    auto get_page(uint64_t id) -> std::optional<const Page*>;
    auto get_page_stats(uint64_t id) -> std::optional<const PageStats*>;
    auto list_pages_of_board_new(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_pages_of_board_top(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_pages_of_user_new(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_pages_of_user_top(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;

    auto get_note(uint64_t id) -> std::optional<const Note*>;
    auto get_note_stats(uint64_t id) -> std::optional<const NoteStats*>;
    auto list_notes_of_post_new(uint64_t post_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_post_top(uint64_t post_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_board_new(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_board_top(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_user_new(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_user_top(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;

    auto get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote;
    //auto list_upvoted_posts_of_user(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    //auto list_downvoted_posts_of_user(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    //auto list_saved_posts_of_user(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    // TODO: Feeds, DMs, Invites, Blocks, Admins/Mods, Mod Actions
  };

  class ReadTxn : public ReadTxnBase {
  protected:
    MDBROTransaction txn;
    auto ro_txn() -> MDBROTransactionImpl& {
      return *txn.get();
    }
  public:
    ReadTxn(DB& db) : ReadTxnBase(db), txn(db.env.getROTransaction()) {}
  };

  class WriteTxn : public ReadTxnBase {
  protected:
    MDBRWTransaction txn;
    auto ro_txn() -> MDBROTransactionImpl& {
      return *txn.get();
    }
    auto get_user_stats_rw(uint64_t id) -> optional<UserStats*>;
    auto get_board_stats_rw(uint64_t id) -> optional<BoardStats*>;
    auto get_page_stats_rw(uint64_t id) -> optional<PageStats*>;
    auto get_note_stats_rw(uint64_t id) -> optional<NoteStats*>;
    auto delete_note_for_page(uint64_t id, uint64_t board_id, std::optional<PageStats*> page_stats) -> bool;
  public:
    WriteTxn(DB& db): ReadTxnBase(db), txn(db.env.getRWTransaction()) {};

    auto next_id() -> uint64_t;
    auto set_setting_str(MDBInVal key, std::string_view value) -> void;
    auto set_setting_int(MDBInVal key, uint64_t value) -> void;

    auto create_user(flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<User> offset) -> uint64_t;
    auto set_user(uint64_t id, flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<User> offset) -> void;
    auto set_local_user(uint64_t id, flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<LocalUser> offset) -> void;
    auto delete_user(uint64_t id) -> bool;

    auto create_board(flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<Board> offset) -> uint64_t;
    auto set_board(uint64_t id, flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<Board> offset) -> void;
    auto set_local_board(uint64_t id, flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<LocalBoard> offset) -> void;
    auto delete_board(uint64_t id) -> bool;
    auto set_subscription(uint64_t user_id, uint64_t board_id, bool subscribed) -> void;

    auto create_page(flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<Page> offset) -> uint64_t;
    auto set_page(uint64_t id, flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<Page> offset) -> void;
    auto delete_page(uint64_t id) -> bool;

    auto create_note(flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<Note> offset) -> uint64_t;
    auto set_note(uint64_t id, flatbuffers::FlatBufferBuilder&& builder, flatbuffers::Offset<Note> offset) -> void;
    auto delete_note(uint64_t id) -> bool;

    auto set_vote(uint64_t user_id, uint64_t post_id, Vote vote) -> void;

    inline auto commit() -> void {
      txn->commit();
    }
  };

  inline auto DB::open_read_txn() -> ReadTxn {
    return ReadTxn(*this);
  }
  inline auto DB::open_write_txn() -> WriteTxn {
    return WriteTxn(*this);
  }
}
