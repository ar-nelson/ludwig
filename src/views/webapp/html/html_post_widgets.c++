#include "html_post_widgets.h++"

using std::pair, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_datetime(ResponseWriter& r, Timestamp timestamp) noexcept {
  return r.write_fmt(R"(<time datetime="{:%FT%TZ}" title="{:%D %r %Z}">{}</time>)"_cf,
    fmt::gmtime(timestamp), fmt::localtime(std::chrono::system_clock::to_time_t(timestamp)), RelativeTime{timestamp});
}

void html_user_avatar(ResponseWriter& r, const User& user, Login login) noexcept {
  if (user.avatar_url() && (!login || login->local_user().show_avatars())) {
    r.write_fmt(R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/user/{}/avatar.webp">)"_cf,
      Escape{user.name()}
    );
  } else {
    r.write(ICON("user"));
  }
}

void html_user_link(ResponseWriter& r, const User& user, bool user_is_admin, Login login, uint64_t board_id) noexcept {
  r.write_fmt(R"(<a class="user-link" href="/u/{}">)"_cf, Escape(user.name()));
  html_user_avatar(r, user, login);
  html_qualified_display_name(r, &user);
  r.write("</a>");
  if (user_is_admin) r.write(R"( <span class="tag tag-admin">Admin</span>)");
  html_tags(r, user);
}

void html_board_icon(ResponseWriter& r, const Board& board) noexcept {
  if (board.icon_url()) {
    r.write_fmt(R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/board/{}/icon.webp">)"_cf,
      Escape(board.name())
    );
  } else {
    r.write(ICON("folder"));
  }
}

void html_board_link(ResponseWriter& r, const Board& board) noexcept {
  r.write_fmt(R"(<a class="board-link" href="/b/{}">)"_cf, Escape(board.name()));
  html_board_icon(r, board);
  html_qualified_display_name(r, &board);
  r.write("</a>");
  html_tags(r, board);
}

template<> auto mod_state_prefix_suffix<ThreadDetail>(ModStateSubject s) noexcept -> pair<string_view, string_view> {
  using enum ModStateSubject;
  switch (s) {
    case Instance: return {"Instance ", ""};
    case Board: return {"Board ", ""};
    case User: return {"User ", " by Admin"};
    case UserInBoard: return {"User ", " by Moderator"};
    case Thread:
    case Comment: return {"", " by Admin"};
    case ThreadInBoard:
    case CommentInBoard: return {"", " by Moderator"};
  }
}
template<> auto mod_state_prefix_suffix<CommentDetail>(ModStateSubject s) noexcept -> pair<string_view, string_view> {
  using enum ModStateSubject;
  switch (s) {
    case Instance: return {"Instance ", ""};
    case Board: return {"Board ", ""};
    case User: return {"User ", " by Admin"};
    case UserInBoard: return {"User ", " by Moderator"};
    case Thread: return {"Thread ", " by Admin"};
    case ThreadInBoard: return {"Thread ", " by Moderator"};
    case Comment: return {"", " by Admin"};
    case CommentInBoard: return {"", " by Moderator"};
  }
}

template<> auto content_warning_prefix<ThreadDetail>(ContentWarningSubject s) noexcept -> string_view {
  return s == ContentWarningSubject::Board ? "Board ": "";
}
template<> auto content_warning_prefix<CommentDetail>(ContentWarningSubject s) noexcept -> string_view {
  switch (s) {
    case ContentWarningSubject::Board: return "Board ";
    case ContentWarningSubject::Thread: return "Thread ";
    default: return "";
  }
}

void html_content_warning(ResponseWriter& r, string_view label, bool is_mod, string_view content, string_view prefix) noexcept {
  r.write_fmt(
    R"(<p class="tag tag-cw content-warning"><strong class="{}-warning-label">{}{}<span class="a11y">:</span></strong> {}</p>)"_cf,
    is_mod ? "mod" : "content", prefix, label, Escape{content}
  );
}

}