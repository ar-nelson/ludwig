#include "util/common.h++"
#include "services/db.h++"
#include "controllers/instance.h++"

using std::optional, std::string;

namespace Ludwig {
  template <unsigned N = 2048>
  static inline auto input_string(optional<string> default_value = {}, optional<std::regex> validation_regex = {}) -> string {
    char buf[N];
    while (true) {
      char* str = fgets(buf, N, stdin);
      const auto len = strlen(str);
      if (str && len > 1) {
        if (validation_regex && !std::regex_match(str, *validation_regex)) {
          puts("ERROR: Invalid value for this field");
        } else {
          assert(str[len - 1] == '\n');
          str[len - 1] = '\0';
          return string(str);
        }
      }
      else if (default_value) return *default_value;
      else puts("ERROR: This field is required");
    }
  }
  static inline auto input_password(size_t min_length = 8) -> SecretString {
    while (true) {
      char* pass = getpass("Password: ");
      if (strlen(pass) >= min_length) {
        SecretString s(pass);
        OPENSSL_cleanse(pass, strlen(pass));
        return s;
      }
      fmt::print("ERROR: Password is too short (min {} characters)", min_length);
    }
  }
  static inline auto input_bool(bool default_value) -> bool {
    char str[3];
    while (true) {
      fgets(str, 2, stdin);
      switch (str[0]) {
        case '\0':
        case ' ':
        case '\n':
          return default_value;
        case 'y':
        case 'Y':
          return true;
        case 'n':
        case 'N':
          return false;
        default:
          puts("ERROR: Must be Y or N");
      }
    }
  }

  auto interactive_setup(bool admin_exists, bool default_board_exists) -> FirstRunSetup {
    FirstRunSetup setup;
    puts("Welcome to Ludwig!");
    puts("------------------\n");

    puts("What is this server's name? [default: Ludwig]");
    setup.name = input_string("Ludwig");

    puts("What domain will this server be accessed at?");
    puts("<NOTE> Include https:// (or http:// if not using SSL for some reason)");
    puts("<IMPORTANT> This cannot be changed later!");
    while (!setup.base_url) {
      if (auto url = Url::parse(input_string())) {
        if (url->is_http_s()) setup.base_url = url->to_string();
        else puts("ERROR: Not an http(s) URL");
      } else puts("ERROR: Invalid URL");
    }

    puts("Allow voting on posts? [Y/n]");
    if (*(setup.votes_enabled = input_bool(true))) {
      puts("Allow downvotes on posts? [Y/n]");
      setup.downvotes_enabled = input_bool(true);
    }

    puts("Allow posts with content warnings (also known as NSFW posts)? [Y/n]");
    setup.cws_enabled = input_bool(true);

    puts("Allow non-admin users to create boards? [Y/n]");
    setup.board_creation_admin_only = !input_bool(true);

    puts("Allow new users to register? [Y/n]");
    if (*(setup.registration_enabled = input_bool(true))) {
      puts("Require admin approval for registration? [Y/n]");
      setup.registration_application_required = input_bool(true);

      puts("Require invite codes for registration? [y/N]");
      if (*(setup.registration_invite_required = input_bool(false))) {
        puts("Allow non-admin users to generate invite codes? [y/N]");
        setup.invite_admin_only = !input_bool(false);
      }
    }

    puts("Require login to view any content on this server? [y/N]");
    setup.require_login_to_view = input_bool(false);

    if (!admin_exists) {
      puts("Create Admin User");
      puts("-----------------\n");
      printf("Username [default: admin]:\n");
      setup.admin_name = input_string<66>("admin", username_regex);
      setup.admin_password = input_password();
    }

    if (!default_board_exists) {
      puts("Create Default Board");
      puts("--------------------\n");
      printf("Name [default: main]:\n");
      setup.default_board_name = input_string<66>("main", username_regex);
    }

    return setup;
  }
}
