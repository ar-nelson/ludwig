#include "first_run_controller.h++"
#include "models/local_user.h++"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <ada.h>

using std::optional, std::string, std::string_view, std::unique_ptr,
  flatbuffers::FlatBufferBuilder;

namespace Ludwig {

auto FirstRunController::first_run_setup(WriteTxn txn, FirstRunSetup&& update, uint64_t as_user) -> void {
  update.validate();
  if (as_user && !LocalUserDetail::get(txn, as_user, {}).local_user().admin()) {
    throw ApiError("Only an admin can perform first-run setup", 403);
  }
  const auto now = now_s();
  if (!txn.get_setting_int(SettingsKey::setup_done)) {
    if (!txn.get_setting_int(SettingsKey::next_id)) txn.set_setting(SettingsKey::next_id, ID_MIN_USER);

    // JWT secret
    uint8_t jwt_secret[JWT_SECRET_SIZE];
    RAND_bytes(jwt_secret, JWT_SECRET_SIZE);
    txn.set_setting(SettingsKey::jwt_secret, string_view{(const char*)jwt_secret, JWT_SECRET_SIZE});
    OPENSSL_cleanse(jwt_secret, JWT_SECRET_SIZE);

    // RSA keys
    const unique_ptr<EVP_PKEY_CTX, void(*)(EVP_PKEY_CTX*)> kctx(
      EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr),
      EVP_PKEY_CTX_free
    );
    EVP_PKEY* key_ptr = nullptr;
    if (
      !kctx ||
      !EVP_PKEY_keygen_init(kctx.get()) ||
      !EVP_PKEY_CTX_set_rsa_keygen_bits(kctx.get(), 2048) ||
      !EVP_PKEY_keygen(kctx.get(), &key_ptr)
    ) {
      throw ApiError("RSA key generation failed (keygen init)", 500);
    }
    const unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)> key(key_ptr, EVP_PKEY_free);
    const unique_ptr<BIO, int(*)(BIO*)>
      bio_public(BIO_new(BIO_s_mem()), BIO_free),
      bio_private(BIO_new(BIO_s_mem()), BIO_free);
    if (
      PEM_write_bio_PUBKEY(bio_public.get(), key.get()) != 1 ||
      PEM_write_bio_PrivateKey(bio_private.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1
    ) {
      throw ApiError("RSA key generation failed (PEM generation)", 500);
    }
    const uint8_t* bio_data;
    size_t bio_len;
    if (!BIO_flush(bio_public.get()) || !BIO_mem_contents(bio_public.get(), &bio_data, &bio_len)) {
      throw ApiError("RSA key generation failed (write public)", 500);
    }
    txn.set_setting(SettingsKey::public_key, string_view{(const char*)bio_data, bio_len});
    if (!BIO_flush(bio_private.get()) || !BIO_mem_contents(bio_private.get(), &bio_data, &bio_len)) {
      throw ApiError("RSA key generation failed (write private)", 500);
    }
    txn.set_setting(SettingsKey::private_key, string_view{(const char*)bio_data, bio_len});

    string url_str(update.base_url.value_or("http://localhost:2023"));
    if (auto url = ada::parse(url_str)) {
      if (!is_https(*url)) {
        throw ApiError("Base URL must start with http:// or https://", 400);
      }
      if ((url->get_pathname_length() != 0 && url->get_pathname() != "/") || url->has_search() || url->has_credentials()) {
        throw ApiError("Base URL must be just a domain; cannot have a path or query parameters", 400);
      }
      txn.set_setting(SettingsKey::base_url, url->get_origin());
    } else throw ApiError("Base URL is not a valid URL (must start with http:// or https://)", 400);

    txn.set_setting(SettingsKey::media_upload_enabled, 0);
    txn.set_setting(SettingsKey::federation_enabled, 0);
    txn.set_setting(SettingsKey::federate_cw_content, 0);
    txn.set_setting(SettingsKey::setup_done, 1);
    txn.set_setting(SettingsKey::created_at, now);
  }
  uint64_t admin = as_user;
  if (update.admin_name && update.admin_password) {
    admin = user_controller->create_local_user(
      txn, *update.admin_name, {}, std::move(*update.admin_password), false, {}, IsApproved::Yes, IsAdmin::Yes
    );
  }
  if (!admin) {
    throw ApiError("Invalid first-run setup: no admin user exists and a new admin was not created", 400);
  }
  if (update.default_board_name) {
    board_controller->create_local_board(txn, admin, *update.default_board_name, {});
  }
# define DEFAULT(KEY, ...) if (!update.KEY) update.KEY.emplace(__VA_ARGS__);
  DEFAULT(name, "Ludwig")
  DEFAULT(description, "A new Ludwig server")
  DEFAULT(icon_url)
  DEFAULT(banner_url)
  DEFAULT(application_question)
  DEFAULT(post_max_length, MiB / 2)
  DEFAULT(remote_post_max_length, MiB)
  DEFAULT(home_page_type, HomePageType::Subscribed)
  DEFAULT(votes_enabled, true)
  DEFAULT(downvotes_enabled, true)
  DEFAULT(cws_enabled, true)
  DEFAULT(javascript_enabled, true)
  DEFAULT(infinite_scroll_enabled, true)
  DEFAULT(board_creation_admin_only, true)
  DEFAULT(registration_enabled, false)
  DEFAULT(registration_application_required, false)
  DEFAULT(registration_invite_required, false)
  DEFAULT(invite_admin_only, true)
  DEFAULT(color_accent, SiteDetail::DEFAULT_COLOR_ACCENT)
  DEFAULT(color_accent_dim, SiteDetail::DEFAULT_COLOR_ACCENT_DIM)
  DEFAULT(color_accent_hover, SiteDetail::DEFAULT_COLOR_ACCENT_HOVER)
  site_controller->update_site(std::move(txn), update, admin);
}

auto FirstRunController::first_run_setup_options(ReadTxn& txn) -> FirstRunSetupOptions {
  return {
    .admin_exists = !txn.get_admin_list().empty(),
    .default_board_exists = !!txn.get_setting_int(SettingsKey::default_board_id),
    .base_url_set = !txn.get_setting_str(SettingsKey::base_url).empty(),
    .home_page_type_set = !!txn.get_setting_int(SettingsKey::home_page_type),
  };
}

template <unsigned N = 2048>
static inline auto input_string(optional<string> default_value = {}, optional<std::regex> validation_regex = {}) -> string {
  char buf[N];
  while (true) {
    char* str = fgets(buf, N, stdin);
    const auto len = strlen(str);
    if (str && len > 1) {
      if (validation_regex && !std::regex_match(str, *validation_regex)) {
        puts("* ERROR: Invalid value for this field");
      } else {
        assert(str[len - 1] == '\n');
        str[len - 1] = '\0';
        return string(str);
      }
    }
    else if (default_value) return *default_value;
    else puts("* ERROR: This field is required");
  }
}

static inline auto input_password(size_t min_length = 8) -> SecretString {
  while (true) {
    char* pass = getpass("* Password: ");
    if (strlen(pass) >= min_length) {
      SecretString s(pass);
      OPENSSL_cleanse(pass, strlen(pass));
      return s;
    }
    fmt::print("* ERROR: Password is too short (min {} characters)", min_length);
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
        puts("* ERROR: Must be Y or N");
    }
  }
}

auto FirstRunController::interactive_setup(bool admin_exists, bool default_board_exists) -> FirstRunSetup {
  static string name, base_url, admin_name, default_board_name;

  FirstRunSetup setup;
  setup.javascript_enabled = true;
  setup.infinite_scroll_enabled = true;

  puts("Welcome to Ludwig!");
  puts("------------------\n");

  puts("* What is this server's name? [default: Ludwig]");
  setup.name = name = input_string("Ludwig");

  puts("* What domain will this server be accessed at?");
  puts("* <NOTE> Include https:// (or http:// if not using SSL for some reason)");
  puts("* <IMPORTANT> This cannot be changed later!");
  while (!setup.base_url) {
    if (auto url = ada::parse(input_string())) {
      if (is_https(*url)) {
        setup.base_url = base_url = url->get_origin();
      }
    } else puts("* ERROR: Invalid URL");
  }

  puts("* Allow voting on posts? [Y/n]");
  if (*(setup.votes_enabled = input_bool(true))) {
    puts("* Allow downvotes on posts? [Y/n]");
    setup.downvotes_enabled = input_bool(true);
  }

  puts("* Allow posts with content warnings (also known as NSFW posts)? [Y/n]");
  setup.cws_enabled = input_bool(true);

  puts("* Allow non-admin users to create boards? [Y/n]");
  setup.board_creation_admin_only = !input_bool(true);

  puts("* Allow new users to register? [Y/n]");
  if (*(setup.registration_enabled = input_bool(true))) {
    puts("* Require admin approval for registration? [Y/n]");
    setup.registration_application_required = input_bool(true);

    puts("* Require invite codes for registration? [y/N]");
    if (*(setup.registration_invite_required = input_bool(false))) {
      puts("* Allow non-admin users to generate invite codes? [y/N]");
      setup.invite_admin_only = !input_bool(false);
    }
  }

  puts("* Require login to view any content on this server? [y/N]");
  setup.require_login_to_view = input_bool(false);

  if (!admin_exists) {
    puts("Create Admin User");
    puts("-----------------\n");
    printf("* Username [default: admin]:\n");
    setup.admin_name = admin_name = input_string<66>("admin", username_regex);
    setup.admin_password = input_password();
    puts("");
  }

  if (!default_board_exists) {
    puts("Create Default Board");
    puts("--------------------\n");
    printf("* Name [default: main]:\n");
    setup.default_board_name = default_board_name = input_string<66>("main", username_regex);
  }

  return setup;
}

}