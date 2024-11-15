#pragma once
#include "util/common.h++"
#include "user_controller.h++"
#include "board_controller.h++"
#include "site_controller.h++"

namespace Ludwig {

struct FirstRunSetup : SiteUpdate {
  std::optional<std::string_view> base_url, default_board_name, admin_name;
  std::optional<SecretString> admin_password;
};

struct FirstRunSetupOptions {
  bool admin_exists, default_board_exists, base_url_set, home_page_type_set;
};

class FirstRunController {
private:
  std::shared_ptr<UserController> user_controller;
  std::shared_ptr<BoardController> board_controller;
  std::shared_ptr<SiteController> site_controller;

public:
  FirstRunController(
    std::shared_ptr<UserController> user,
    std::shared_ptr<BoardController> board,
    std::shared_ptr<SiteController> site
  ) : user_controller(user), board_controller(board), site_controller(site) {
    assert(user != nullptr);
    assert(board != nullptr);
    assert(site != nullptr);
  }

  static auto interactive_setup(bool admin_exists, bool default_board_exists) -> FirstRunSetup;
  static auto first_run_setup_options(ReadTxn& txn) -> FirstRunSetupOptions;
  auto first_run_setup(WriteTxn txn, FirstRunSetup&& update, uint64_t as_user = 0) -> void;
};

}