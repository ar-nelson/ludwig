#pragma once
#include "db/db.h++"
#include "models/local_user.h++"
#include "models/thread.h++"
#include "models/comment.h++"

namespace Ludwig {

struct NotificationDetail {
  uint64_t id;
  std::reference_wrapper<const Notification> notification;
  std::variant<
    std::monostate,
    ThreadDetail,
    CommentDetail,
    std::reference_wrapper<const Board>,
    std::reference_wrapper<const User>
  > subject = {};

  static auto get(ReadTxn& txn, uint64_t id, const LocalUserDetail& login) -> NotificationDetail;
};

}