#include "test_common.h++"
#include "services/db.h++"
#include "controllers/instance.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset;

#define HOUR 3600
#define DAY (HOUR * 24)

struct Instance {
  TempFile file;
  shared_ptr<InstanceController> controller;

  Instance() {
    auto epoch = now_s() - DAY * 7;
    auto db = make_shared<DB>(file.name);
    auto txn = db->open_write_txn();
    txn.set_setting(SettingsKey::created_at, epoch);
    txn.set_setting(SettingsKey::base_url, "http://ludwig.test");
    txn.commit();
    controller = make_shared<InstanceController>(db, nullptr);
  }
};

#define NUM_THREADS 7

struct PopulatedInstance {
  TempFile file;
  shared_ptr<InstanceController> controller;
  uint64_t users[6], boards[3], threads[NUM_THREADS];

  PopulatedInstance() {
    auto epoch = now_s() - DAY * 7;
    auto db = make_shared<DB>(file.name);
    auto txn = db->open_write_txn();
    txn.set_setting(SettingsKey::created_at, epoch);
    txn.set_setting(SettingsKey::base_url, "http://ludwig.test");
    FlatBufferBuilder fbb;
    fbb.ForceDefaults(true);
    {
      const auto name = fbb.CreateString("admin");
      const auto [dn_t, dn] = plain_text_to_rich_text(fbb, "Admin User");
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_display_name_type(dn_t);
      u.add_display_name(dn);
      u.add_created_at(epoch);
      fbb.Finish(u.Finish());
    }
    users[0] = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      auto email = fbb.CreateString("admin@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_admin(true);
      lu.add_approved(true);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[0], fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto name = fbb.CreateString("rando"),
        bio_raw = fbb.CreateString("Check out my website: [click here!](http://rando.example) :partyparrot:");
      const auto [dn_t, dn] = plain_text_to_rich_text(fbb, "Some Local Rando");
      const auto bio_t = fbb.CreateVector(vector{RichText::Text, RichText::Link, RichText::Text, RichText::Emoji, RichText::Text});
      const auto bio = fbb.CreateVector(vector{
        fbb.CreateString("<p>Check out my website: ").Union(),
        fbb.CreateString("http://rando.example").Union(),
        fbb.CreateString("click here!</a> ").Union(),
        fbb.CreateString("partyparrot").Union(),
        fbb.CreateString("</p>").Union()
      });
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_display_name_type(dn_t);
      u.add_display_name(dn);
      u.add_bio_type(bio_t);
      u.add_bio(bio);
      u.add_created_at(epoch + HOUR);
      u.add_updated_at(epoch + DAY * 2);
      fbb.Finish(u.Finish());
    }
    users[1] = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      auto email = fbb.CreateString("rando@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(true);
      lu.add_show_bot_accounts(false);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[1], fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto name = fbb.CreateString("troll"),
        bio_raw = fbb.CreateSharedString("Problem?"),
        mod_reason = fbb.CreateString("begone");
      const auto [dn_t, dn] = plain_text_to_rich_text(fbb, "Banned Troll");
      const auto [bio_t, bio] = plain_text_to_rich_text(fbb, "Problem?");
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_display_name_type(dn_t);
      u.add_display_name(dn);
      u.add_bio_type(bio_t);
      u.add_bio(bio);
      u.add_created_at(epoch + DAY);
      u.add_mod_state(ModState::Removed);
      u.add_mod_reason(mod_reason);
      fbb.Finish(u.Finish());
    }
    users[2] = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      auto email = fbb.CreateString("troll@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(true);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[2], fbb.GetBufferSpan());
    txn.set_hide_user(users[2], users[0], true);
    fbb.Clear();
    {
      const auto name = fbb.CreateString("robot"),
        bio_raw = fbb.CreateSharedString("domo");
      const auto [dn_t, dn] = plain_text_to_rich_text(fbb, "Mr. Roboto");
      const auto [bio_t, bio] = plain_text_to_rich_text(fbb, "domo");
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_display_name_type(dn_t);
      u.add_display_name(dn);
      u.add_bio_type(bio_t);
      u.add_bio(bio);
      u.add_created_at(epoch + DAY + HOUR * 2);
      u.add_bot(true);
      fbb.Finish(u.Finish());
    }
    users[3] = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      auto email = fbb.CreateString("robot@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(true);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[3], fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto name = fbb.CreateString("visitor@federated.test"),
        actor_url = fbb.CreateString("https://federated.test/ap/user/visitor"),
        inbox_url = fbb.CreateString("https://federated.test/ap/user/visitor/inbox");
      const auto [dn_t, dn] = plain_text_to_rich_text(fbb, "Visitor from Elsewhere");
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_display_name_type(dn_t);
      u.add_display_name(dn);
      u.add_instance(1);
      u.add_actor_id(actor_url);
      u.add_inbox_url(inbox_url);
      u.add_created_at(epoch + DAY + HOUR);
      fbb.Finish(u.Finish());
    }
    users[4] = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto name = fbb.CreateString("unapproved");
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_created_at(epoch + DAY * 5);
      fbb.Finish(u.Finish());
    }
    users[5] = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto email = fbb.CreateString("unapproved@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(false);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[5], fbb.GetBufferSpan());
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
      fbb.Clear();
      const auto name = fbb.CreateString(fmt::format("filler_u{}@federated.test", i)),
        actor_url = fbb.CreateString(fmt::format("https://federated.test/ap/user/filler_u{}", i)),
        inbox_url = fbb.CreateString(fmt::format("https://federated.test/ap/user/filler_u{}/inbox", i));
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_actor_id(actor_url);
      u.add_inbox_url(inbox_url);
      u.add_instance(1);
      u.add_created_at(epoch + DAY * 6 + i);
      fbb.Finish(u.Finish());
      txn.create_user(fbb.GetBufferSpan());
    }
    fbb.Clear();
    {
      const auto name = fbb.CreateString("foo");
      BoardBuilder b(fbb);
      b.add_name(name);
      b.add_created_at(epoch);
      fbb.Finish(b.Finish());
    }
    boards[0] = txn.create_board(fbb.GetBufferSpan());
    fbb.Clear();
    {
      LocalBoardBuilder b(fbb);
      b.add_owner(users[0]);
      fbb.Finish(b.Finish());
    }
    txn.set_local_board(boards[0], fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto name = fbb.CreateString("bar");
      BoardBuilder b(fbb);
      b.add_name(name);
      b.add_created_at(epoch + 2 * DAY);
      b.add_approve_subscribe(true);
      b.add_restricted_posting(true);
      fbb.Finish(b.Finish());
    }
    boards[1] = txn.create_board(fbb.GetBufferSpan());
    fbb.Clear();
    {
      LocalBoardBuilder b(fbb);
      b.add_owner(users[1]);
      fbb.Finish(b.Finish());
    }
    txn.set_local_board(boards[1], fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto name = fbb.CreateString("baz@federated.test"),
        actor_url = fbb.CreateString("https://federated.test/ap/group/baz"),
        inbox_url = fbb.CreateString("https://federated.test/ap/group/baz/inbox");
      BoardBuilder b(fbb);
      b.add_name(name);
      b.add_actor_id(actor_url);
      b.add_inbox_url(inbox_url);
      b.add_instance(1);
      b.add_created_at(epoch + DAY);
      fbb.Finish(b.Finish());
    }
    boards[2] = txn.create_board(fbb.GetBufferSpan());
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
      fbb.Clear();
      const auto name = fbb.CreateString(fmt::format("filler_b{}@federated.test", i)),
        actor_url = fbb.CreateString(fmt::format("https://federated.test/ap/group/filler_b{}", i)),
        inbox_url = fbb.CreateString(fmt::format("https://federated.test/ap/group/filler_b{}/inbox", i));
      BoardBuilder b(fbb);
      b.add_name(name);
      b.add_actor_id(actor_url);
      b.add_inbox_url(inbox_url);
      b.add_instance(1);
      b.add_created_at(epoch + DAY * 6 + i);
      fbb.Finish(b.Finish());
      txn.create_board(fbb.GetBufferSpan());
    }
    //               board    user     time      title        content_warning        mod_state
    const std::tuple<uint8_t, uint8_t, uint64_t, string_view, optional<string_view>, optional<ModState>> thread_data[NUM_THREADS] = {
      {0, 0, 0, "Hello, world!", {}, {}},
      {0, 1, HOUR, "Another post", {}, {}},
      {0, 1, HOUR * 2, "cthulhu fhtagn", "may summon cthulhu", {}},
      {0, 2, DAY, "I am going to cause problems on purpose", {}, {}},
      {0, 3, DAY + HOUR, "beep boop", {}, {}},
      {0, 4, DAY * 2, "Is a hot dog a taco?", {}, ModState::Locked},
      {0, 4, DAY * 3, "Is a Pop-Tart a calzone?", {}, ModState::Removed}
    };
    for (size_t i = 0; i < NUM_THREADS; i++) {
      fbb.Clear();
      const auto [board, user, time, title_raw, content_warning, mod_state] = thread_data[i];
      const auto url_s = fbb.CreateString("https://example.com"),
        content_warning_s = content_warning ? fbb.CreateString(*content_warning) : 0;
      const auto [title_type, title] = plain_text_to_rich_text(fbb, title_raw);
      ThreadBuilder t(fbb);
      t.add_board(boards[board]);
      t.add_author(users[user]);
      t.add_created_at(epoch + time);
      t.add_title_type(title_type);
      t.add_title(title);
      t.add_content_url(url_s);
      if (content_warning) t.add_content_warning(content_warning_s);
      if (mod_state) t.add_mod_state(*mod_state);
      fbb.Finish(t.Finish());
      threads[i] = txn.create_thread(fbb.GetBufferSpan());
    }
    for (size_t i = 0; i < ITEMS_PER_PAGE * 3; i++) {
      fbb.Clear();
      const auto url_s = fbb.CreateString("https://example.com");
      const auto [title_type, title] = plain_text_to_rich_text(fbb, fmt::format("filler post {}", i));
      ThreadBuilder t(fbb);
      t.add_board(boards[1]);
      t.add_author(users[3]);
      t.add_created_at(epoch + DAY * 3 + HOUR * i);
      t.add_title_type(title_type);
      t.add_title(title);
      t.add_content_url(url_s);
      fbb.Finish(t.Finish());
    }
    txn.commit();
    controller = make_shared<InstanceController>(db, nullptr);
  }
};

TEST_CASE("hash password", "[instance]") {
  static constexpr string_view
    salt = "0123456789abcdef",
    password = "fhqwhgads",
    expected_hash = "3e7bdeadbcbede063612b1ced9c42852848d088c4bfa5ed160862d168ec11e99";

  uint8_t hash[32];
  InstanceController::hash_password({ std::string(password) }, reinterpret_cast<const uint8_t*>(salt.data()), hash);
  string actual_hash;
  for(size_t i = 0; i < 32; i++) {
    fmt::format_to(std::back_inserter(actual_hash), "{:02x}", hash[i]);
  }
  REQUIRE(actual_hash == expected_hash);
}

TEST_CASE_METHOD(PopulatedInstance, "list users", "[instance]") {
  auto txn = controller->open_read_txn();
  vector<string> vec;
  const auto add_name = [&](auto& i){vec.emplace_back(i.user().name()->str());};

  // New, not logged in, local and federated
  auto next = controller->list_users(add_name, txn, UserSortType::New, false);
  {
    vector<string> tmp;
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
      tmp.emplace_back(fmt::format("filler_u{}@federated.test", ITEMS_PER_PAGE - (i + 1)));
    }
    CHECK(vec == tmp);
  }
  CHECK(next);
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::New, false, {}, next);
  CHECK(vec == vector<string>{
    "robot", "visitor@federated.test", "rando", "admin"
  });
  CHECK_FALSE(next);

  // New, not logged in, local only
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::New, true);
  CHECK(vec == vector<string>{"robot", "rando", "admin"});
  CHECK_FALSE(next);

  // Old, not logged in, local and federated
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::Old, false);
  CHECK(vec.size() == ITEMS_PER_PAGE);
  vec.resize(5);
  CHECK(vec == vector<string>{
    "admin", "rando", "visitor@federated.test", "robot", "filler_u0@federated.test"
  });
  REQUIRE(next);
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::Old, false, {}, next);
  CHECK(vec.size() == 4);
  CHECK_FALSE(next);

  // Old, not logged in, local only
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::Old, true);
  CHECK(vec == vector<string>{"admin", "rando", "robot"});
  CHECK_FALSE(next);

  // New, logged in as admin, local only
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::New, true, LocalUserDetail::get_login(txn, users[0]));
  CHECK(vec == vector<string>{"unapproved", "robot", "troll", "rando", "admin"});
  CHECK_FALSE(next);

  // New, logged in as rando (excludes bots), local only
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::New, true, LocalUserDetail::get_login(txn, users[1]));
  CHECK(vec == vector<string>{"rando", "admin"});
  CHECK_FALSE(next);

  // New, logged in as troll (includes self, hides admin), local only
  vec.clear();
  next = controller->list_users(add_name, txn, UserSortType::New, true, LocalUserDetail::get_login(txn, users[2]));
  CHECK(vec == vector<string>{"robot", "troll", "rando"});
  CHECK_FALSE(next);
}

TEST_CASE_METHOD(Instance, "register and login", "[instance]") {
  // Try registration (forbidden by default)
  REQUIRE_THROWS_AS(
    controller->register_local_user(
      "somebody",
      "somebody@example.test",
      {"foobarbaz"},
      "0.0.0.0",
      "internet exploder -1"
    ),
    ApiError
  );

  // Enable registration, then it should work
  controller->update_site({
    .registration_enabled = true,
    .registration_application_required = false,
    .registration_invite_required = false
  }, {});
  pair<uint64_t, bool> result;
  REQUIRE_NOTHROW(
    result = controller->register_local_user(
      "somebody",
      "somebody@example.test",
      {"foobarbaz"},
      "0.0.0.0",
      "internet exploder -1"
    )
  );
  const auto [id, approved] = result;
  REQUIRE(id > 0);
  CHECK(approved);

  // get created user
  {
    auto txn = controller->open_read_txn();
    const auto u = LocalUserDetail::get_login(txn, id);
    REQUIRE(u.id == id);
    CHECK(u.user().name()->string_view() == "somebody");
    CHECK(u.local_user().email()->string_view() == "somebody@example.test");
    CHECK(u.local_user().approved());
    CHECK_FALSE(u.local_user().accepted_application());
    CHECK_FALSE(u.local_user().email_verified());
    CHECK_FALSE(u.local_user().invite());
  }

  // login with wrong password
  CHECK_THROWS_AS(
    controller->login("somebody", {"foobarbazqux"}, "0.0.0.0", "internet exploder -1"),
    ApiError
  );

  // login with wrong username
  CHECK_THROWS_AS(
    controller->login("somebodyy", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"),
    ApiError
  );

  SECTION("login attempts") {
    LoginResponse login;
    SECTION("login with correct password (by username)") {
      REQUIRE_NOTHROW(login = controller->login("somebody", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"));
    }
    SECTION("login with correct password (by email)") {
      REQUIRE_NOTHROW(login = controller->login("somebody@example.test", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"));
    }
    SECTION("login with correct password (by username, case-insensitive)") {
      REQUIRE_NOTHROW(login = controller->login("sOmEbOdY", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"));
    }
    SECTION("login with correct password (by email, case-insensitive)") {
      REQUIRE_NOTHROW(login = controller->login("SOMEBODY@EXAMPLE.TEST", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"));
    }

    CHECK(login.user_id == id);
    CHECK(login.expiration > std::chrono::system_clock::now());

    auto txn = controller->open_read_txn();
    CHECK(controller->validate_session(txn, login.session_id) == optional(id));
  }
}

TEST_CASE_METHOD(Instance, "register with application", "[instance]") {
  controller->update_site({
    .registration_enabled = true,
    .registration_application_required = true,
    .registration_invite_required = false
  }, {});

  // Try registration with no application
  REQUIRE_THROWS_AS(
    controller->register_local_user(
      "somebody",
      "somebody@example.test",
      {"foobarbaz"},
      "0.0.0.0",
      "internet exploder -1"
    ),
    ApiError
  );

  // Now try with application
  pair<uint64_t, bool> result;
  REQUIRE_NOTHROW(
    result = controller->register_local_user(
      "somebody",
      "somebody@example.test",
      {"foobarbaz"},
      "0.0.0.0",
      "internet exploder -1",
      {},
      "please let me into the forum\n\ni am normal and can be trusted with post"
    )
  );
  const auto [id, approved] = result;
  REQUIRE(id > 0);
  CHECK_FALSE(approved);

  {
    auto txn = controller->open_read_txn();
    const auto u = LocalUserDetail::get_login(txn, id);
    REQUIRE(u.id == id);
    CHECK(u.user().name()->string_view() == "somebody");
    CHECK(u.local_user().email()->string_view() == "somebody@example.test");
    CHECK_FALSE(u.local_user().approved());
    CHECK_FALSE(u.local_user().accepted_application());
    CHECK_FALSE(u.local_user().email_verified());
    CHECK_FALSE(u.local_user().invite());

    const auto a_opt = txn.get_application(id);
    REQUIRE(a_opt);
    const auto& a = a_opt->get();
    CHECK(a.ip()->string_view() == "0.0.0.0");
    CHECK(a.user_agent()->string_view() == "internet exploder -1");
    CHECK(a.text()->string_view() == "please let me into the forum\n\ni am normal and can be trusted with post");
  }

  REQUIRE_NOTHROW(controller->approve_local_user_application(id, {}));

  {
    auto txn = controller->open_read_txn();
    const auto u = LocalUserDetail::get_login(txn, id);
    CHECK(u.user().name()->string_view() == "somebody");
    CHECK(u.local_user().email()->string_view() == "somebody@example.test");
    CHECK(u.local_user().approved());
    CHECK(u.local_user().accepted_application());
  }
}

TEST_CASE_METHOD(PopulatedInstance, "register with invite", "[instance]") {
  controller->update_site({
    .registration_enabled = true,
    .registration_application_required = false,
    .registration_invite_required = true
  }, {});

  // Try registration with no invite
  REQUIRE_THROWS_AS(
    controller->register_local_user(
      "somebody",
      "somebody@example.test",
      {"foobarbaz"},
      "0.0.0.0",
      "internet exploder -1"
    ),
    ApiError
  );

  // Create invite from admin
  const auto invite = controller->create_site_invite(users[0]);

  // Now try with invite
  pair<uint64_t, bool> result;
  REQUIRE_NOTHROW(
    result = controller->register_local_user(
      "somebody",
      "somebody@example.test",
      {"foobarbaz"},
      "0.0.0.0",
      "internet exploder -1",
      invite
    )
  );
  const auto [id, approved] = result;
  REQUIRE(id > 0);
  CHECK(approved);

  auto txn = controller->open_read_txn();
  const auto u = LocalUserDetail::get_login(txn, id);
  REQUIRE(u.id == id);
  CHECK(u.user().name()->string_view() == "somebody");
  CHECK(u.local_user().email()->string_view() == "somebody@example.test");
  CHECK(u.local_user().approved());
  CHECK_FALSE(u.local_user().accepted_application());
  CHECK_FALSE(u.local_user().email_verified());
  CHECK(u.local_user().invite() == optional(invite));

  const auto i_opt = txn.get_invite(invite);
  REQUIRE(i_opt);
  const auto& i = i_opt->get();
  CHECK(i.accepted_at() > 0);
  CHECK(i.accepted_at() <= now_s());
  CHECK(i.from() == users[0]);
}
