#pragma once
#include "html_common.h++"
#include "html_rich_text.h++"
#include "fbs/records.h++"
#include "models/site.h++"
#include "models/local_user.h++"
#include "models/thread.h++"
#include "models/comment.h++"
#include "models/enums.h++"

namespace Ludwig {

template <class T>
void html_qualified_display_name(ResponseWriter& r, const T* it) noexcept {
  const auto name = it->name()->string_view();
  if (it->display_name_type() && it->display_name_type()->size()) {
    r.write(rich_text_to_html_emojis_only(it->display_name_type(), it->display_name(), {}));
    const auto at_index = name.find('@');
    if (at_index != std::string_view::npos) r.write(name.substr(at_index));
  } else {
    r.write(name);
  }
}

static inline auto describe_mod_state(ModState s) -> std::string_view {
  switch (s) {
    case ModState::Flagged: return "Flagged";
    case ModState::Locked: return "Locked";
    case ModState::Unapproved: return "Not Approved";
    case ModState::Removed: return "Removed";
    default: return "";
  }
}

void html_datetime(ResponseWriter& r, Timestamp timestamp) noexcept;

void html_user_avatar(ResponseWriter& r, const User& user, Login login = {}) noexcept;

void html_user_link(ResponseWriter& r, const User& user, bool user_is_admin, Login login, uint64_t board_id = 0) noexcept;

void html_board_icon(ResponseWriter& r, const Board& board) noexcept;

void html_board_link(ResponseWriter& r, const Board& board) noexcept;

template <class T> 
void html_vote_buttons(ResponseWriter& r, const T& entry, const SiteDetail* site, Login login) noexcept {
  using fmt::operator""_cf;
  const auto can_upvote = entry.can_upvote(login, site),
    can_downvote = entry.can_downvote(login, site);
  if (can_upvote || can_downvote) {
    r.write_fmt(
      R"(<form class="vote-buttons" id="votes-{0:x}" method="post" action="/{1}/{0:x}/vote" hx-post="/{1}/{0:x}/vote" hx-swap="outerHTML">)"_cf,
      entry.id, T::noun
    );
  } else {
    r.write_fmt(R"(<div class="vote-buttons" id="votes-{:x}">)"_cf, entry.id);
  }
  if (entry.should_show_votes(login, site)) {
    if (!login || login->local_user().show_karma()) {
      r.write_fmt(R"(<output class="karma" id="karma-{:x}">{}</output>)"_cf, entry.id, Suffixed{entry.stats().karma()});
    } else {
      r.write(R"(<div class="karma">&nbsp;</div>)");
    }
    r.write_fmt(
      R"(<label class="upvote"><button type="submit" name="vote" {0}{2}>)"
      ICON("chevron-up") R"(<span class="a11y">Upvote</span></button></label>)"
      R"(<label class="downvote"><button type="submit" name="vote" {1}{3}>)"
      ICON("chevron-down") R"(<span class="a11y">Downvote</span></button></label>)"_cf,
      can_upvote ? "" : "disabled ", can_downvote ? "" : "disabled ",
      entry.your_vote == Vote::Upvote ? R"(class="voted" value="0")" : R"(value="1")",
      entry.your_vote == Vote::Downvote ? R"(class="voted" value="0")" : R"(value="-1")"
    );
  }
  r.write((can_upvote || can_downvote) ? "</form>" : "</div>");
}

template<class T> auto mod_state_prefix_suffix(ModStateSubject s) noexcept -> std::pair<std::string_view, std::string_view> {
  using enum ModStateSubject;
  switch (s) {
    case Instance: return {"Instance ", ""};
    case Board:
    case User:
    case Thread:
    case Comment: return {"", " by Admin"};
    case UserInBoard:
    case ThreadInBoard:
    case CommentInBoard: return {"", " by Moderator"};
  }
}
template<> auto mod_state_prefix_suffix<ThreadDetail>(ModStateSubject s) noexcept -> std::pair<std::string_view, std::string_view>;
template<> auto mod_state_prefix_suffix<CommentDetail>(ModStateSubject s) noexcept -> std::pair<std::string_view, std::string_view>;

template<class T> auto content_warning_prefix(ContentWarningSubject s) noexcept -> std::string_view {
  return "";
}
template<> auto content_warning_prefix<ThreadDetail>(ContentWarningSubject s) noexcept -> std::string_view;
template<> auto content_warning_prefix<CommentDetail>(ContentWarningSubject s) noexcept -> std::string_view;

void html_content_warning(ResponseWriter& r, std::string_view label, bool is_mod, std::string_view content, std::string_view prefix = "") noexcept;

template <class T>
void html_content_warnings(ResponseWriter& r, const T& post, PostContext context) noexcept {
  using fmt::operator""_cf;
  const auto mod_state = post.mod_state(context);
  r.write(R"(<p class="content-warning">)");
  if (
    mod_state.state > ModState::Normal &&
    (context == PostContext::View || context == PostContext::Reply || mod_state.subject >= ModStateSubject::ThreadInBoard)
  ) {
    const auto [prefix, suffix] = mod_state_prefix_suffix<T>(mod_state.subject);
    if (mod_state.reason) {
      html_content_warning(
        r,
        fmt::format("{}{}{}"_cf, prefix, describe_mod_state(mod_state.state), suffix),
        true,
        *mod_state.reason
      );
    } else {
      r.write_fmt(
        R"(<span class="tag tag-mod-state">{}{}{}</span>)"_cf,
        prefix, describe_mod_state(mod_state.state), suffix
      );
    }
  }
  if (const auto cw = post.content_warning(context)) {
    if (context == PostContext::View || context == PostContext::Reply || cw->subject >= ContentWarningSubject::Thread) {
      const auto prefix = content_warning_prefix<T>(cw->subject);
      html_content_warning(r, "Content Warning", false, cw->content_warning, prefix);
    }
  }
  r.write("</p>");
}

template<class T>
requires requires (T t) { t.mod_state(); }
void html_tags(ResponseWriter& r, const T& record) noexcept {
  using fmt::operator""_cf;
  if (record.deleted_at()) {
    r.write(R"( <span class="tag tag-deleted">Deleted</span>)");
  }
  if constexpr (requires { record.bot(); }) {
    if (record.bot()) r.write(R"( <span class="tag tag-bot">Bot</span>)");
  }
  if constexpr (requires { record.content_warning(); }) {
    if (record.content_warning()) {
      r.write_fmt(R"( <abbr class="tag tag-cw" title="Content Warning: {}">CW</abbr>)"_cf, Escape(record.content_warning()));
    }
  }
  // TODO: board-specific mod_state
  if (record.mod_state() > ModState::Normal) {
    if (record.mod_reason()) {
      r.write_fmt(R"( <abbr class="tag tag-mod-state" title="{0}: {1}">{0}</abbr>)"_cf,
        describe_mod_state(record.mod_state()),
        Escape(record.mod_reason())
      );
    } else {
      r.write_fmt(R"( <span class="tag tag-mod-state">{}</span>)"_cf, describe_mod_state(record.mod_state()));
    }
  }
}

template<class T>
requires requires (T t, PostContext c) { t.mod_state(c); }
void html_tags(ResponseWriter& r, const T& detail, PostContext context) noexcept {
  using fmt::operator""_cf;
  // TODO: mark Deleted
  if constexpr (requires { detail.user().bot(); }) {
    if (detail.user().bot()) r.write(R"( <span class="tag tag-bot">Bot</span>)");
  }
  const auto mod_state = detail.mod_state(context);
  if (mod_state.state > ModState::Normal) {
    auto [prefix, suffix] = mod_state_prefix_suffix<T>(mod_state.subject);
    r.write_fmt(R"( <abbr class="tag tag-mod-state" title="{0}{1}{2}{3}{4}">{1}</abbr>)"_cf,
      prefix, describe_mod_state(mod_state.state), suffix, mod_state.reason ? ": " : "", Escape(mod_state.reason.value_or(""))
    );
  }
  if constexpr (requires { detail.content_warning(context); }) {
    if (const auto cw = detail.content_warning(context)) {
      const auto prefix = content_warning_prefix<T>(cw->subject);
      r.write_fmt(R"( <abbr class="tag tag-cw" title="{}Content Warning: {}">CW</abbr>)"_cf,
        prefix, Escape(cw->content_warning)
      );
    }
  }
}

}