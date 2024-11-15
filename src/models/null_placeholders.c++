#include "null_placeholders.h++"
#include "util/common.h++"

namespace Ludwig {

PlaceholderFlatbuffers::PlaceholderFlatbuffers() {
  fbb.ForceDefaults(true);
  const auto blank = fbb.CreateString("");
  const auto display_name_type = fbb.CreateVector(std::vector{RichText::Text});
  const auto display_name = fbb.CreateVector(std::vector{fbb.CreateString("[deleted]").Union()});
  const auto admin = fbb.CreateString("admin");
  {
    LinkCardBuilder card(fbb);
    auto offset = card.Finish();
    null_link_card = get_temporary_pointer(fbb, offset);
  }
  {
    BoardBuilder board(fbb);
    board.add_name(blank);
    board.add_display_name_type(display_name_type);
    board.add_display_name(display_name);
    board.add_can_upvote(false);
    board.add_can_downvote(false);
    auto offset = board.Finish();
    null_board = get_temporary_pointer(fbb, offset);
  }
  {
    UserBuilder user(fbb);
    user.add_name(blank);
    user.add_display_name_type(display_name_type);
    user.add_display_name(display_name);
    auto offset = user.Finish();
    null_user = get_temporary_pointer(fbb, offset);
  }
  {
    UserBuilder user(fbb);
    user.add_name(admin);
    auto offset = user.Finish();
    temp_admin_user = get_temporary_pointer(fbb, offset);
  }
  {
    LocalUserBuilder user(fbb);
    user.add_admin(true);
    auto offset = user.Finish();
    temp_admin_local_user = get_temporary_pointer(fbb, offset);
  }
  {
    UserStatsBuilder stats(fbb);
    auto offset = stats.Finish();
    temp_admin_stats = get_temporary_pointer(fbb, offset);
  }
  {
    LocalUserStatsBuilder stats(fbb);
    auto offset = stats.Finish();
    temp_admin_local_stats = get_temporary_pointer(fbb, offset);
  }
}

const PlaceholderFlatbuffers placeholders = PlaceholderFlatbuffers();

}