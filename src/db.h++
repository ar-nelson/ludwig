#pragma once
#include "iter.h++"
#include "generated/datatypes_generated.h"

namespace Ludwig {
  enum Sort {
    Active,
    Hot,
    New,
    Old,
    MostComments,
    NewComments,
    TopAll,
    TopYear,
    TopSixMonths,
    TopThreeMonths,
    TopMonth,
    TopWeek,
    TopDay,
    TopTwelveHour,
    TopSixHour,
    TopHour
  };

  enum Vote {
    Downvote = -1,
    NoVote = 0,
    Upvote = 1
  };

  class ReadTxn;
  class ReadTxnImpl;
  class WriteTxn;

  class DB {
  private:
    MDBEnv env;
    MDBDbi dbis[128];
  public:
    uint64_t seed;
    DB(const char* filename);
    auto open_read_txn() -> ReadTxnImpl;
    auto open_write_txn() -> WriteTxn;
    friend class ReadTxn;
    friend class ReadTxnImpl;
    friend class WriteTxn;
  };

  class ReadTxn {
  protected:
    DB& db;
    virtual auto ro_txn() -> MDBROTransactionImpl& = 0;
  public:
    ReadTxn(DB& db) : db(db) {}

    auto get_user_id(std::string_view name) -> std::optional<uint64_t>;
    auto get_user(uint64_t id) -> std::optional<const User*>;
    auto get_local_user(uint64_t id) -> std::optional<const LocalUser*>;
    auto count_local_users() -> uint64_t;
    auto list_users(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_local_users(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_subscribers(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto user_is_subscribed(uint64_t user_id, uint64_t board_id) -> bool;

    auto get_board_id(std::string_view name) -> std::optional<uint64_t>;
    auto get_board(uint64_t id) -> std::optional<const Board*>;
    auto get_local_board(uint64_t name) -> std::optional<const LocalBoard*>;
    auto count_local_boards() -> uint64_t;
    auto list_boards(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_local_boards(const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_subscribed_boards(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_created_boards(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;

    auto get_page(uint64_t id) -> std::optional<const Page*>;
    auto count_pages_of_board(uint64_t board_id) -> uint64_t;
    auto count_pages_of_user(uint64_t user_id) -> uint64_t;
    auto list_pages_of_board_new(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_pages_of_board_top(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_pages_of_user_new(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_pages_of_user_top(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;

    auto get_note(uint64_t id) -> std::optional<const Note*>;
    auto count_notes_of_post(uint64_t post_id) -> uint64_t;
    auto count_notes_of_user(uint64_t user_id) -> uint64_t;
    auto list_notes_of_post_new(uint64_t post_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_post_top(uint64_t post_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_board_new(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_board_top(uint64_t board_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_user_new(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    auto list_notes_of_user_top(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;

    auto count_karma_of_post(uint64_t post_id) -> int64_t;
    auto count_karma_of_user(uint64_t user_id) -> int64_t;
    auto get_vote_of_user_for_post(uint64_t user_id, uint64_t post_id) -> Vote;
    //auto list_upvoted_posts_of_user(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    //auto list_downvoted_posts_of_user(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    //auto list_saved_posts_of_user(uint64_t user_id, const std::optional<Cursor>& cursor = {}) -> DBIter<uint64_t>;
    // TODO: Feeds, DMs, Invites, Blocks, Admins/Mods, Mod Actions
  };

  class ReadTxnImpl : public ReadTxn {
  protected:
    MDBROTransaction txn;
    auto ro_txn() -> MDBROTransactionImpl& {
      return *txn.get();
    }
  public:
    ReadTxnImpl(DB& db) : ReadTxn(db), txn(db.env.getROTransaction()) {}
  };

  class WriteTxn : public ReadTxn {
  protected:
    MDBRWTransaction txn;
    auto ro_txn() -> MDBROTransactionImpl& {
      return *txn.get();
    }
  public:
    WriteTxn(DB& db): ReadTxn(db), txn(db.env.getRWTransaction()) {};

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

  inline auto DB::open_read_txn() -> ReadTxnImpl {
    return ReadTxnImpl(*this);
  }
  inline auto DB::open_write_txn() -> WriteTxn {
    return WriteTxn(*this);
  }
}
