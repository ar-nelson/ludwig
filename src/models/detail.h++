#pragma once
#include "util/common.h++"
#include "services/db.h++"

namespace Ludwig {

  enum class HomePageType : uint8_t {
    Subscribed = 1,
    Local,
    All,
    BoardList,
    SingleBoard
  };

  struct SiteDetail {
    static inline const std::string
      DEFAULT_COLOR_ACCENT = "#1077c1", // hsl(205, 85%, 41%)
      DEFAULT_COLOR_ACCENT_DIM = "#73828c", // hsl(205, 10%, 50%)
      DEFAULT_COLOR_ACCENT_HOVER = "#085e9b", // hsl(205, 90%, 32%)
      DEFAULT_NAME = "Ludwig",
      DEFAULT_BASE_URL = "http://localhost:2023";

    std::string name, base_url, description, public_key_pem, color_accent, color_accent_dim, color_accent_hover;
    std::optional<std::string> icon_url, banner_url, application_question;
    HomePageType home_page_type;
    uint64_t default_board_id, post_max_length, created_at, updated_at;
    bool setup_done, javascript_enabled, infinite_scroll_enabled, votes_enabled,
        downvotes_enabled, cws_enabled, require_login_to_view,
        board_creation_admin_only, registration_enabled,
        registration_application_required, registration_invite_required,
        invite_admin_only;

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
    auto can_change_settings(Login login) const noexcept -> bool;

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
    static const LinkCard* const null_link_card;
    static const User* const null_user;
    static const Board* const null_board;

    uint64_t id;
    double rank;
    Vote your_vote;
    bool saved, hidden, user_hidden, board_hidden, board_subscribed;
    std::reference_wrapper<const Thread> _thread;
    std::reference_wrapper<const PostStats> _stats;
    OptRef<LinkCard> _link_card;
    OptRef<User> _author;
    OptRef<Board> _board;

    auto thread() const noexcept -> const Thread& { return _thread; }
    auto stats() const noexcept -> const PostStats& { return _stats; }
    auto link_card() const noexcept -> const LinkCard& {
      return _link_card ? _link_card->get() : *null_link_card;
    }
    auto author() const noexcept -> const User& {
      return _author ? _author->get() : *null_user;
    }
    auto board() const noexcept -> const Board& {
      return _board ? _board->get() : *null_board;
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
    auto can_upvote(Login login, const SiteDetail* site) const noexcept -> bool;
    auto can_downvote(Login login, const SiteDetail* site) const noexcept -> bool;
    auto should_show_votes(Login login, const SiteDetail* site) const noexcept -> bool;
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
    static const User* const null_user;
    static const Thread* const null_thread;
    static const Board* const null_board;

    uint64_t id;
    double rank;
    Vote your_vote;
    bool saved, hidden, thread_hidden, user_hidden, board_hidden, board_subscribed;
    std::reference_wrapper<const Comment> _comment;
    std::reference_wrapper<const PostStats> _stats;
    OptRef<User> _author;
    OptRef<Thread> _thread;
    OptRef<Board> _board;
    std::vector<uint64_t> path;

    auto comment() const noexcept -> const Comment& { return _comment; }
    auto stats() const noexcept -> const PostStats& { return _stats; }
    auto author() const noexcept -> const User& {
      return _author ? _author->get() : *null_user;
    }
    auto thread() const noexcept -> const Thread& {
      return _thread ? _thread->get() : *null_thread;
    }
    auto board() const noexcept -> const Board& {
      return _board ? _board->get() : *null_board;
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
    auto can_upvote(Login login, const SiteDetail* site) const noexcept -> bool;
    auto can_downvote(Login login, const SiteDetail* site) const noexcept -> bool;
    auto should_show_votes(Login login, const SiteDetail* site) const noexcept -> bool;

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
    static const User* const temp_admin_user;
    static const LocalUser* const temp_admin_local_user;
    static const UserStats* const temp_admin_stats;
    static auto temp_admin() -> LocalUserDetail {
      return {{ 0, *temp_admin_user, std::reference_wrapper(*temp_admin_local_user), *temp_admin_stats, false }};
    }

    inline auto local_user() const -> const LocalUser& { return _local_user->get(); }

    static auto get(ReadTxnBase& txn, uint64_t id, Login login) -> LocalUserDetail;
    static auto get_login(ReadTxnBase& txn, uint64_t id) -> LocalUserDetail;
    static auto get_login(ReadTxnBase& txn, std::optional<uint64_t> id) -> std::optional<LocalUserDetail> {
      return id.transform([&](auto id){return get_login(txn, id);});
    }
  };

  struct LocalBoardDetail : BoardDetail {
    inline auto local_board() const -> const LocalBoard& { return _local_board->get(); }

    static auto get(ReadTxnBase& txn, uint64_t id, Login login) -> LocalBoardDetail;
  };
}
