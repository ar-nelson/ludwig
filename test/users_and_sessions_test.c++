#include "test_common.h++"
#include "db/db.h++"
#include "controllers/session_controller.h++"
#include "util/rich_text.h++"
#include <numeric>

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset;

#define HOUR 3600
#define DAY (HOUR * 24)

struct Instance {
  TempFile file;
  shared_ptr<DB> db;
  shared_ptr<SiteController> site_c;
  shared_ptr<UserController> user_c;
  shared_ptr<SessionController> session_c;

  Instance() {
    auto epoch = now_s() - DAY * 7;
    db = make_shared<DB>(file.name, 100, true);
    auto txn = db->open_write_txn_sync();
    txn.set_setting(SettingsKey::created_at, epoch);
    txn.set_setting(SettingsKey::base_url, "http://ludwig.test");
    txn.commit();
    site_c = make_shared<SiteController>(db);
    user_c = make_shared<UserController>(site_c);
    session_c = make_shared<SessionController>(db, site_c, user_c);
  }
};

struct PopulatedInstance : public Instance {
  uint64_t users[6], boards[2];

  PopulatedInstance() : Instance() {
    auto epoch = now_s() - DAY * 7;
    db = make_shared<DB>(file.name, 100, true);
    auto txn = db->open_write_txn_sync();
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
      u.add_bio_raw(bio_raw);
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
      u.add_bio_raw(bio_raw);
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
      u.add_bio_raw(bio_raw);
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
      u.add_mod_state(ModState::Unapproved);
      fbb.Finish(u.Finish());
    }
    users[5] = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto email = fbb.CreateString("unapproved@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[5], fbb.GetBufferSpan());
    for (int i = 0; i < 20; i++) {
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
    /*
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
    for (int i = 0; i < 20; i++) {
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
    for (size_t i = 0; i < 20 * 3; i++) {
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
    */
    txn.commit();
  }
};

TEST_CASE("hash password", "[session_controller]") {
  static constexpr string_view
    salt = "0123456789abcdef",
    password = "fhqwhgads",
    expected_hash = "3e7bdeadbcbede063612b1ced9c42852848d088c4bfa5ed160862d168ec11e99";

  uint8_t hash[32];
  UserController::hash_password({ std::string(password) }, reinterpret_cast<const uint8_t*>(salt.data()), hash);
  string actual_hash;
  for(size_t i = 0; i < 32; i++) {
    fmt::format_to(std::back_inserter(actual_hash), "{:02x}", hash[i]);
  }
  REQUIRE(actual_hash == expected_hash);
}

TEST_CASE_METHOD(PopulatedInstance, "list users", "[user_controller]") {
  auto txn = db->open_read_txn();
  vector<string> vec;
  auto populate_vec = [&](std::generator<const UserDetail&> users) {
    vec.clear();
    uint8_t i = 0;
    for (auto& u : users) {
      vec.emplace_back(u.user().name()->str());
      if (++i >= 20) break;
    }
    spdlog::info("[{}]", std::accumulate(std::next(vec.cbegin()), vec.cend(), vec[0], [](const string& a, const string& b) { return a + ", " + b; }));
  };

  // New, not logged in, local and federated
  PageCursor next;
  populate_vec(user_c->list_users(txn, next, UserSortType::New, false));
  {
    vector<string> tmp;
    for (int i = 0; i < 20; i++) {
      tmp.emplace_back(fmt::format("filler_u{}@federated.test", 20 - (i + 1)));
    }
    CHECK(vec == tmp);
  }
  CHECK(next);
  populate_vec(user_c->list_users(txn, next, UserSortType::New, false));
  CHECK(vec == vector<string>{
    "robot", "visitor@federated.test", "rando", "admin"
  });
  CHECK_FALSE(next);

  // New, not logged in, local only
  next.reset();
  populate_vec(user_c->list_users(txn, next, UserSortType::New, true));
  CHECK(vec == vector<string>{"robot", "rando", "admin"});
  CHECK_FALSE(next);

  // Old, not logged in, local and federated
  next.reset();
  populate_vec(user_c->list_users(txn, next, UserSortType::Old, false));
  CHECK(vec.size() == 20);
  vec.resize(5);
  CHECK(vec == vector<string>{
    "admin", "rando", "visitor@federated.test", "robot", "filler_u0@federated.test"
  });
  REQUIRE(next);
  populate_vec(user_c->list_users(txn, next, UserSortType::Old, false));
  CHECK(vec.size() == 4);
  CHECK_FALSE(next);

  // Old, not logged in, local only
  next.reset();
  populate_vec(user_c->list_users(txn, next, UserSortType::Old, true));
  CHECK(vec == vector<string>{"admin", "rando", "robot"});
  CHECK_FALSE(next);

  // New, logged in as admin, local only
  next.reset();
  populate_vec(user_c->list_users(txn, next, UserSortType::New, true, LocalUserDetail::get_login(txn, users[0])));
  CHECK(vec == vector<string>{"unapproved", "robot", "troll", "rando", "admin"});
  CHECK_FALSE(next);

  // New, logged in as rando (excludes bots), local only
  next.reset();
  populate_vec(user_c->list_users(txn, next, UserSortType::New, true, LocalUserDetail::get_login(txn, users[1])));
  CHECK(vec == vector<string>{"rando", "admin"});
  CHECK_FALSE(next);

  // New, logged in as troll (includes self, hides admin), local only
  next.reset();
  populate_vec(user_c->list_users(txn, next, UserSortType::New, true, LocalUserDetail::get_login(txn, users[2])));
  CHECK(vec == vector<string>{"robot", "troll", "rando"});
  CHECK_FALSE(next);
}

TEST_CASE_METHOD(Instance, "register and login", "[session_controller]") {
  // Try registration (forbidden by default)
  {
    auto txn = db->open_write_txn_sync();
    CHECK_THROWS_AS(
      session_c->register_local_user(
        txn,
        "nobody",
        "nobody@example.test",
        {"foobarbaz"},
        "0.0.0.0",
        "internet exploder -1"
      ),
      ApiError
    );
  }

  // Enable registration, then it should work
  pair<uint64_t, bool> result;
  {
    site_c->update_site(db->open_write_txn_sync(), {
      .registration_enabled = true,
      .registration_application_required = false,
      .registration_invite_required = false
    }, {});
    auto txn = db->open_write_txn_sync();
    REQUIRE_NOTHROW(
      result = session_c->register_local_user(
        txn,
        "somebody",
        "somebody@example.test",
        {"foobarbaz"},
        "0.0.0.0",
        "internet exploder -1"
      )
    );
    txn.commit();
  }
  const auto [id, approved] = result;
  REQUIRE(id > 0);
  CHECK(approved);

  // get created user
  {
    auto txn = db->open_read_txn();
    const auto u = LocalUserDetail::get_login(txn, id);
    REQUIRE(u.id == id);
    CHECK(u.user().name()->string_view() == "somebody");
    CHECK(u.local_user().email()->string_view() == "somebody@example.test");
    CHECK(u.user().mod_state() == ModState::Normal);
    CHECK_FALSE(u.local_user().accepted_application());
    CHECK_FALSE(u.local_user().email_verified());
    CHECK_FALSE(u.local_user().invite());
  }

  // login with wrong password
  {
    auto txn = db->open_write_txn_sync();
    CHECK_THROWS_AS(
      session_c->login(txn, "somebody", {"foobarbazqux"}, "0.0.0.0", "internet exploder -1"),
      ApiError
    );
  }

  // login with wrong username
  {
    auto txn = db->open_write_txn_sync();
    CHECK_THROWS_AS(
      session_c->login(txn, "somebodyy", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"),
      ApiError
    );
  }

  SECTION("login attempts") {
    LoginResponse login;
    SECTION("login with correct password (by username)") {
      auto txn = db->open_write_txn_sync();
      REQUIRE_NOTHROW(login = session_c->login(
        txn, "somebody", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"
      ));
      txn.commit();
    }
    SECTION("login with correct password (by email)") {
      auto txn = db->open_write_txn_sync();
      REQUIRE_NOTHROW(login = session_c->login(
        txn, "somebody@example.test", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"
      ));
      txn.commit();
    }
    SECTION("login with correct password (by username, case-insensitive)") {
      auto txn = db->open_write_txn_sync();
      REQUIRE_NOTHROW(login = session_c->login(
        txn, "sOmEbOdY", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"
      ));
      txn.commit();
    }
    SECTION("login with correct password (by email, case-insensitive)") {
      auto txn = db->open_write_txn_sync();
      REQUIRE_NOTHROW(login = session_c->login(
        txn, "SOMEBODY@EXAMPLE.TEST", {"foobarbaz"}, "0.0.0.0", "internet exploder -1"
      ));
      txn.commit();
    }

    CHECK(login.user_id == id);
    CHECK(login.expiration > now_t());

    auto txn = db->open_read_txn();
    CHECK(session_c->validate_session(txn, login.session_id) == optional(id));
  }
}

TEST_CASE_METHOD(Instance, "register with application", "[session_controller]") {
  pair<uint64_t, bool> result;
  {
    site_c->update_site(db->open_write_txn_sync(), {
      .registration_enabled = true,
      .registration_application_required = true,
      .registration_invite_required = false
    }, {});

    auto txn = db->open_write_txn_sync();

    // Try registration with no application
    REQUIRE_THROWS_AS(
      session_c->register_local_user(
        txn,
        "somebody",
        "somebody@example.test",
        {"foobarbaz"},
        "0.0.0.0",
        "internet exploder -1"
      ),
      ApiError
    );

    // Now try with application
    REQUIRE_NOTHROW(
      result = session_c->register_local_user(
        txn,
        "somebody",
        "somebody@example.test",
        {"foobarbaz"},
        "0.0.0.0",
        "internet exploder -1",
        {},
        "please let me into the forum\n\ni am normal and can be trusted with post"
      )
    );
    txn.commit();
  }
  const auto [id, approved] = result;
  REQUIRE(id > 0);
  CHECK_FALSE(approved);

  {
    auto txn = db->open_read_txn();
    const auto u = LocalUserDetail::get_login(txn, id);
    REQUIRE(u.id == id);
    CHECK(u.user().name()->string_view() == "somebody");
    CHECK(u.local_user().email()->string_view() == "somebody@example.test");
    CHECK(u.user().mod_state() == ModState::Unapproved);
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

  {
    auto txn = db->open_write_txn_sync();
    REQUIRE_NOTHROW(session_c->approve_local_user_application(txn, id, {}));

    const auto u = LocalUserDetail::get_login(txn, id);
    CHECK(u.user().name()->string_view() == "somebody");
    CHECK(u.local_user().email()->string_view() == "somebody@example.test");
    CHECK(u.user().mod_state() == ModState::Approved);
    CHECK(u.local_user().accepted_application());
    txn.commit();
  }
}

TEST_CASE_METHOD(PopulatedInstance, "register with invite", "[session_controller]") {
  pair<uint64_t, bool> result;
  uint64_t invite;
  site_c->update_site(db->open_write_txn_sync(), {
    .registration_enabled = true,
    .registration_application_required = false,
    .registration_invite_required = true
  }, {});

  {
    auto txn = db->open_write_txn_sync();
    // Try registration with no invite
    REQUIRE_THROWS_AS(
      session_c->register_local_user(
        txn,
        "somebody",
        "somebody@example.test",
        {"foobarbaz"},
        "0.0.0.0",
        "internet exploder -1"
      ),
      ApiError
    );

    // Create invite from admin
    invite = session_c->create_site_invite(txn, users[0]);

    // Now try with invite
    REQUIRE_NOTHROW(
      result = session_c->register_local_user(
        txn,
        "somebody",
        "somebody@example.test",
        {"foobarbaz"},
        "0.0.0.0",
        "internet exploder -1",
        invite
      )
    );
    txn.commit();
  }
  const auto [id, approved] = result;
  REQUIRE(id > 0);
  CHECK(approved);

  auto txn = db->open_read_txn();
  const auto u = LocalUserDetail::get_login(txn, id);
  REQUIRE(u.id == id);
  CHECK(u.user().name()->string_view() == "somebody");
  CHECK(u.local_user().email()->string_view() == "somebody@example.test");
  CHECK(u.user().mod_state() == ModState::Normal);
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
