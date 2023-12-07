#pragma once
#include "util/common.h++"
#include "services/db.h++"

namespace Ludwig {

  struct SiteDetail {
    std::string name, domain, description;
    std::optional<std::string> icon_url, banner_url;
    uint64_t max_post_length;
    bool javascript_enabled, infinite_scroll_enabled, board_creation_admin_only,
         registration_enabled, registration_application_required,
         registration_invite_required, invite_admin_only;

    static auto get(ReadTxnBase& txn) -> SiteDetail;
  };

  struct LocalUserDetail;
  using Login = const std::optional<LocalUserDetail>&;

  struct UserDetail {
    uint64_t id;
    std::reference_wrapper<const User> _user;
    OptRef<const LocalUser> _local_user;
    std::reference_wrapper<const UserStats> _stats;
    bool hidden;

    auto user() const noexcept -> const User& { return _user; }
    auto maybe_local_user() const noexcept -> OptRef<const LocalUser> { return _local_user; }
    auto stats() const noexcept -> const UserStats& { return _stats; }
    auto mod_state() const noexcept -> ModState { return user().mod_state(); }
    auto created_at() const noexcept -> std::chrono::system_clock::time_point {
      return std::chrono::system_clock::time_point(std::chrono::seconds(user().created_at()));
    }

    auto can_view(Login login) const noexcept -> bool;
    auto should_show(Login login) const noexcept -> bool;

    static constexpr std::string_view noun = "user";
    static auto get(ReadTxnBase& txn, uint64_t id, Login login) -> UserDetail;
  };

  struct BoardDetail {
    uint64_t id;
    std::reference_wrapper<const Board> _board;
    OptRef<const LocalBoard> _local_board;
    std::reference_wrapper<const BoardStats> _stats;
    bool hidden, subscribed;

    auto board() const noexcept -> const Board& { return _board; }
    auto maybe_local_board() const noexcept -> OptRef<const LocalBoard> { return _local_board; }
    auto stats() const noexcept -> const BoardStats& { return _stats; }
    auto mod_state() const noexcept -> ModState { return board().mod_state(); }
    auto created_at() const noexcept -> std::chrono::system_clock::time_point {
      return std::chrono::system_clock::time_point(std::chrono::seconds(board().created_at()));
    }

    auto can_view(Login login) const noexcept -> bool;
    auto should_show(Login login) const noexcept -> bool;
    auto can_create_thread(Login login) const noexcept -> bool;
    auto can_change_settings(Login login) const noexcept -> bool;

    static constexpr std::string_view noun = "board";
    static auto get(ReadTxnBase& txn, uint64_t id, Login login) -> BoardDetail;
  };

  struct ThreadDetail {
  private:
    static const flatbuffers::FlatBufferBuilder null_user, null_board, null_link_card;
  public:
    uint64_t id;
    double rank;
    Vote your_vote;
    bool saved, hidden, user_hidden, board_hidden;
    std::reference_wrapper<const Thread> _thread;
    std::reference_wrapper<const PostStats> _stats;
    OptRef<LinkCard> _link_card;
    OptRef<User> _author;
    OptRef<Board> _board;

    auto thread() const noexcept -> const Thread& { return _thread; }
    auto stats() const noexcept -> const PostStats& { return _stats; }
    auto link_card() const noexcept -> const LinkCard& {
      return _link_card ? _link_card->get() : *flatbuffers::GetRoot<LinkCard>(null_link_card.GetBufferPointer());
    }
    auto author() const noexcept -> const User& {
      return _author ? _author->get() : *flatbuffers::GetRoot<User>(null_user.GetBufferPointer());
    }
    auto board() const noexcept -> const Board& {
      return _board ? _board->get() : *flatbuffers::GetRoot<Board>(null_board.GetBufferPointer());
    }
    auto mod_state() const noexcept -> ModState { return thread().mod_state(); }
    auto created_at() const noexcept -> std::chrono::system_clock::time_point {
      return std::chrono::system_clock::time_point(std::chrono::seconds(thread().created_at()));
    }
    auto author_id() const noexcept -> uint64_t { return thread().author(); }

    auto can_view(Login login) const noexcept -> bool;
    auto should_show(Login login) const noexcept -> bool;
    auto can_reply_to(Login login) const noexcept -> bool;
    auto can_edit(Login login) const noexcept -> bool;
    auto can_delete(Login login) const noexcept -> bool;
    auto can_upvote(Login login) const noexcept -> bool;
    auto can_downvote(Login login) const noexcept -> bool;
    auto should_fetch_card() const noexcept -> bool;

    static constexpr std::string_view noun = "thread";
    static auto get(
      ReadTxnBase& txn,
      uint64_t thread_id,
      Login login,
      OptRef<User> author = {},
      bool is_author_hidden = false,
      OptRef<Board> board = {},
      bool is_board_hidden = false
    ) -> ThreadDetail;
    static auto get_created_at(
      ReadTxnBase& txn,
      uint64_t id
    ) -> std::chrono::system_clock::time_point {
      using namespace std::chrono;
      const auto thread = txn.get_thread(id);
      if (!thread) return system_clock::time_point::min();
      return system_clock::time_point(seconds(thread->get().created_at()));
    }
  };

  struct CommentDetail {
  private:
    static const flatbuffers::FlatBufferBuilder null_user, null_thread, null_board;
  public:
    uint64_t id;
    double rank;
    Vote your_vote;
    bool saved, hidden, thread_hidden, user_hidden, board_hidden;
    std::reference_wrapper<const Comment> _comment;
    std::reference_wrapper<const PostStats> _stats;
    OptRef<User> _author;
    OptRef<Thread> _thread;
    OptRef<Board> _board;

    auto comment() const noexcept -> const Comment& { return _comment; }
    auto stats() const noexcept -> const PostStats& { return _stats; }
    auto author() const noexcept -> const User& {
      return _author ? _author->get() : *flatbuffers::GetRoot<User>(null_user.GetBufferPointer());
    }
    auto thread() const noexcept -> const Thread& {
      return _thread ? _thread->get() : *flatbuffers::GetRoot<Thread>(null_thread.GetBufferPointer());
    }
    auto board() const noexcept -> const Board& {
      return _board ? _board->get() : *flatbuffers::GetRoot<Board>(null_board.GetBufferPointer());
    }
    auto mod_state() const noexcept -> ModState { return comment().mod_state(); }
    auto created_at() const noexcept -> std::chrono::system_clock::time_point {
      return std::chrono::system_clock::time_point(std::chrono::seconds(comment().created_at()));
    }
    auto author_id() const noexcept -> uint64_t { return comment().author(); }

    auto can_view(Login login) const noexcept -> bool;
    auto should_show(Login login) const noexcept -> bool;
    auto can_reply_to(Login login) const noexcept -> bool;
    auto can_edit(Login login) const noexcept -> bool;
    auto can_delete(Login login) const noexcept -> bool;
    auto can_upvote(Login login) const noexcept -> bool;
    auto can_downvote(Login login) const noexcept -> bool;

    static constexpr std::string_view noun = "comment";
    static auto get(
      ReadTxnBase& txn,
      uint64_t comment_id,
      Login login,
      OptRef<User> author = {},
      bool is_author_hidden = false,
      OptRef<Thread> thread = {},
      bool is_thread_hidden = false,
      OptRef<Board> board = {},
      bool is_board_hidden = false
    ) -> CommentDetail;
    static auto get_created_at(
      ReadTxnBase& txn,
      uint64_t id
    ) -> std::chrono::system_clock::time_point {
      using namespace std::chrono;
      const auto comment = txn.get_comment(id);
      if (!comment) return system_clock::time_point::min();
      return system_clock::time_point(seconds(comment->get().created_at()));
    }
  };

  struct LocalUserDetail : UserDetail {
    inline auto local_user() const -> const LocalUser& { return _local_user->get(); }

    static auto get(ReadTxnBase& txn, uint64_t id, Login login = {}) -> LocalUserDetail;
  };

  struct LocalBoardDetail : BoardDetail {
    inline auto local_board() const -> const LocalBoard& { return _local_board->get(); }

    static auto get(ReadTxnBase& txn, uint64_t id, Login login) -> LocalBoardDetail;
  };
}
