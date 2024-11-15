#include "notification.h++"

namespace Ludwig {

auto NotificationDetail::get(
  ReadTxn& txn,
  uint64_t notification_id,
  const LocalUserDetail& login
) -> NotificationDetail {
  using enum NotificationType;
  const auto notification = txn.get_notification(notification_id);
  if (!notification || notification->get().user() != login.id) {
    throw ApiError("Notification does not exist", 410);
  }
  NotificationDetail detail { .id = notification_id, .notification = *notification };
  auto subject = notification->get().subject();
  switch (notification->get().type()) {
    case MentionInThread:
    case BoostThread:
      detail.subject = ThreadDetail::get(txn, subject.value(), login);
      break;
    case MentionInComment:
    case ReplyToThread:
    case ReplyToComment:
    case BoostComment:
      detail.subject = CommentDetail::get(txn, subject.value(), login);
      break;
    case ApproveSubscription:
    case BecomeMod:
    case SubscribedBoardRemoved:
    case SubscribedBoardDefederated:
      if (subject) {
        if (auto board = txn.get_board(*subject)) {
          detail.subject = *board;
        }
      }
      break;
    case Follow:
    case FollowedUserRemoved:
    case FollowedUserDefererated:
      if (auto user = txn.get_user(subject.value())) {
        detail.subject = *user;
      }
      break;
  }
  return detail;
}

}