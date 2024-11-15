#pragma once
#include "html_common.h++"
#include "views/webapp/webapp_common.h++"
#include "db/page_cursor.h++"
#include "models/notification.h++"

namespace Ludwig {

void html_notification(ResponseWriter& r, const NotificationDetail& notification, Login login = {}) noexcept;

void html_notification_list(
  GenericContext& r,
  PageCursor& cursor,
  std::generator<const NotificationDetail&> entries
);

}