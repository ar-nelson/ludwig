#pragma once
#include "util/common.h++"
#include "fbs/records.h++"

namespace Ludwig {

enum class HomePageType : uint8_t {
  Subscribed = 1,
  Local,
  All,
  BoardList,
  SingleBoard
};

enum class ModStateSubject : uint8_t {
  Instance,
  Board,
  UserInBoard,
  User,
  ThreadInBoard,
  Thread,
  CommentInBoard,
  Comment
};

struct ModStateDetail {
  ModStateSubject subject = ModStateSubject::Instance;
  ModState state = ModState::Normal;
  std::optional<std::string_view> reason = {};
};

}