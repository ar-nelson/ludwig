#pragma once
#include "fbs/records.h++"

namespace Ludwig {

class PlaceholderFlatbuffers {
private:
  flatbuffers::FlatBufferBuilder fbb;

public:
  const LinkCard* null_link_card;
  const Board* null_board;
  const Thread* null_thread;
  const User* null_user, *temp_admin_user;
  const LocalUser* temp_admin_local_user;
  const UserStats* temp_admin_stats;
  const LocalUserStats* temp_admin_local_stats;

  PlaceholderFlatbuffers();
};

extern const PlaceholderFlatbuffers placeholders;

}