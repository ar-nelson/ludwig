#include "html_notification_list.h++"
#include "html_rich_text.h++"
#include "html_post_widgets.h++"
#include "html_list_widgets.h++"

using std::generator, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_notification(ResponseWriter& r, const NotificationDetail& detail, Login login) noexcept {
    using enum NotificationType;
    const auto& notification = detail.notification.get();
    r.write_fmt(
      R"(<li class="notification{}" id="notification-{:x}"><div class="notification-body">)"_cf,
      notification.read_at() ? "" : " unread-notification",
      detail.id
    );
    try {
      switch (notification.type()) {
        case MentionInThread: {
          const auto& thread = std::get<ThreadDetail>(detail.subject);
          html_user_link(r, thread.author(), false, login, thread.thread().board());
          r.write_fmt(
            R"( mentioned you in <strong><a href="/thread/{:x}">{}</a></strong></div>)"
            R"(<div class="notification-summary">{}</div>)"_cf,
            thread.id,
            rich_text_to_html_emojis_only(thread.thread().title_type(), thread.thread().title()),
            rich_text_to_html(thread.thread().content_text_type(), thread.thread().content_text())
          );
          break;
        }
        case MentionInComment: {
          const auto& comment = std::get<CommentDetail>(detail.subject);
          html_user_link(r, comment.author(), false, login, comment.thread().board());
          r.write_fmt(
            R"( mentioned you in <a href="/comment/{:x}">a reply</a> to <strong><a href="/thread/{:x}">{}</a></strong></div>)"
            R"(<div class="notification-summary">{}</div>)"_cf,
            comment.id,
            comment.comment().thread(),
            rich_text_to_html_emojis_only(comment.thread().title_type(), comment.thread().title()),
            rich_text_to_html(comment.comment().content_type(), comment.comment().content())
          );
          break;
        }
        case ReplyToThread: {
          const auto& comment = std::get<CommentDetail>(detail.subject);
          html_user_link(r, comment.author(), false, login, comment.thread().board());
          r.write_fmt(
            R"( posted <a href="/comment/{:x}">a comment</a> on your thread <strong><a href="/thread/{:x}">{}</a></strong></div>)"
            R"(<div class="notification-summary">{}</div>)"_cf,
            comment.id,
            comment.comment().thread(),
            rich_text_to_html_emojis_only(comment.thread().title_type(), comment.thread().title()),
            rich_text_to_html(comment.comment().content_type(), comment.comment().content())
          );
          break;
        }
        case ReplyToComment: {
          const auto& comment = std::get<CommentDetail>(detail.subject);
          html_user_link(r, comment.author(), false, login, comment.thread().board());
          r.write_fmt(
            R"( posted <a href="/comment/{:x}">a comment</a> on your thread <strong><a href="/thread/{:x}">{}</a></strong></div>)"
            R"(<div class="notification-summary">{}</div>)"_cf,
            comment.id,
            comment.comment().thread(),
            rich_text_to_html_emojis_only(comment.thread().title_type(), comment.thread().title()),
            rich_text_to_html(comment.comment().content_type(), comment.comment().content())
          );
          break;
        }
        default:
          r.write("This notification type is not yet implemented.</div>");
      }
    } catch (...) {
      r.write("Error displaying notification.</div>");
    }
    if (!notification.read_at()) {
      r.write_fmt(
        R"(<form class="notification-buttons" action="/notifications/{0:x}/read" method="post" hx-post="/notifications/{0:x}/read" hx-target="notification-{0:x}">)"
        R"(<button type="submit">Mark as read</button></form></li>)"_cf,
        detail.id
      );
    }
    r.write("</li>");
}

void html_notification_list(
  GenericContext& r,
  PageCursor& cursor,
  generator<const NotificationDetail&> entries
) {
  bool is_first_page = !cursor.exists;
  if (!r.is_htmx) {
    r.write(
      R"(<div><form action="/notifications/all_read" method="post" hx-action="/notifications/all_read" hx-target="#top-level-list">)"
      R"(<button type="submit">Mark all as read</button></form><main>)"
      R"(<ol class="notification-list" id="top-level-list">)"
    );
  }
  bool any_entries = false;
  for (const auto& n : entries) {
    html_notification(r, n, r.login);
    any_entries = true;
  }
  if (!r.is_htmx) {
    if (!any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
    r.write("</ol>");
  }
  html_pagination(r, "/notifications", is_first_page, cursor);
  if (!r.is_htmx) r.write("</main>");
}

}