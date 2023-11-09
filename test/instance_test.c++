#include "test_common.h++"
#include "services/db.h++"
#include "controllers/instance.h++"
#include <sstream>
#include <iomanip>

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset;

#define HOUR 3600
#define DAY (HOUR * 24)

auto rich_text = std::make_shared<RichTextParser>(std::make_shared<LibXmlContext>());

struct Instance {
  TempFile file;
  shared_ptr<InstanceController> controller;
  uint64_t users[6], boards[3];

  Instance() {
    auto epoch = now_s() - DAY * 7;
    auto db = make_shared<DB>(file.name);
    auto txn = db->open_write_txn();
    txn.set_setting(SettingsKey::created_at, epoch);
    txn.set_setting(SettingsKey::domain, "ludwig.test");
    txn.set_setting(SettingsKey::registration_application_required, 1);
    FlatBufferBuilder fbb;
    fbb.ForceDefaults(true);
    {
      const auto name = fbb.CreateString("admin");
      const auto [dn_t, dn] = RichTextParser::plain_text_to_plain_text_with_emojis(fbb, "Admin User");
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
        bio_raw = fbb.CreateString("This is *my* bio");
      const auto [dn_t, dn] = RichTextParser::plain_text_to_plain_text_with_emojis(fbb, "Some Local Rando");
      const auto bio_t = fbb.CreateVector(vector{TextBlock::P});
      const auto bio = fbb.CreateVector(vector{CreateTextSpans(fbb,
        fbb.CreateVector(vector{TextSpan::Plain, TextSpan::Italic, TextSpan::Plain}),
        fbb.CreateVector(vector{
          fbb.CreateString("This is ").Union(),
          CreateTextSpans(fbb,
            fbb.CreateVector(vector{TextSpan::Plain}),
            fbb.CreateVector(vector{fbb.CreateString("my").Union()})
          ).Union(),
          fbb.CreateString(" bio").Union()
        })
      ).Union()});
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
      const auto [dn_t, dn] = RichTextParser::plain_text_to_plain_text_with_emojis(fbb, "Banned Troll");
      const auto [bio_t, bio] = RichTextParser::plain_text_to_blocks(fbb, "Problem?");
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
      const auto [dn_t, dn] = RichTextParser::plain_text_to_plain_text_with_emojis(fbb, "Mr. Roboto");
      const auto [bio_t, bio] = RichTextParser::plain_text_to_blocks(fbb, "domo");
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
      const auto [dn_t, dn] = RichTextParser::plain_text_to_plain_text_with_emojis(fbb, "Visitor from Elsewhere");
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
      auto email = fbb.CreateString("unapproved@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(false);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[5], fbb.GetBufferSpan());
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
      fbb.Clear();
      const auto name = fbb.CreateString(fmt::format("filler{}@federated.test", i)),
        actor_url = fbb.CreateString(fmt::format("https://federated.test/ap/user/filler{}", i)),
        inbox_url = fbb.CreateString(fmt::format("https://federated.test/ap/user/filler{}/inbox", i));
      UserBuilder u(fbb);
      u.add_name(name);
      u.add_actor_id(actor_url);
      u.add_inbox_url(inbox_url);
      u.add_instance(1);
      u.add_created_at(epoch + DAY * 6 + i);
      fbb.Finish(u.Finish());
      txn.create_user(fbb.GetBufferSpan());
    }
    txn.commit();
    controller = make_shared<InstanceController>(db, nullptr, rich_text);
  }

  inline auto operator->() -> InstanceController* {
    return controller.get();
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

TEST_CASE("list users", "[instance]") {
  Instance instance;
  auto txn = instance->open_read_txn();
  vector<string> vec;

  // New, not logged in, local and federated
  auto page = instance->list_users(txn, UserSortType::New, false);
  REQUIRE(page.entries.size() > 0);
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  {
    vector<string> tmp;
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
      tmp.emplace_back(fmt::format("filler{}@federated.test", ITEMS_PER_PAGE - (i + 1)));
    }
    REQUIRE(vec == tmp);
  }
  REQUIRE(page.is_first);
  REQUIRE(!!page.next);
  page = instance->list_users(txn, UserSortType::New, false, {}, page.next);
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{
    "robot", "visitor@federated.test", "rando", "admin"
  });
  REQUIRE(!page.is_first);
  REQUIRE(!page.next);

  // New, not logged in, local only
  page = instance->list_users(txn, UserSortType::New, true);
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"robot", "rando", "admin"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);

  // Old, not logged in, local and federated
  page = instance->list_users(txn, UserSortType::Old, false);
  REQUIRE(page.entries.size() == ITEMS_PER_PAGE);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  vec.resize(5);
  REQUIRE(vec == vector<string>{
    "admin", "rando", "visitor@federated.test", "robot", "filler0@federated.test"
  });
  REQUIRE(page.is_first);
  REQUIRE(!!page.next);
  page = instance->list_users(txn, UserSortType::Old, false, {}, page.next);
  REQUIRE(page.entries.size() == 4);
  REQUIRE(!page.is_first);
  REQUIRE(!page.next);

  // Old, not logged in, local only
  page = instance->list_users(txn, UserSortType::Old, true);
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"admin", "rando", "robot"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);

  // New, logged in as admin, local only
  page = instance->list_users(txn, UserSortType::New, true, LocalUserDetail::get(txn, instance.users[0]));
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"unapproved", "robot", "troll", "rando", "admin"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);

  // New, logged in as rando (excludes bots), local only
  page = instance->list_users(txn, UserSortType::New, true, LocalUserDetail::get(txn, instance.users[1]));
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"rando", "admin"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);

  // New, logged in as troll (includes self, hides admin), local only
  page = instance->list_users(txn, UserSortType::New, true, LocalUserDetail::get(txn, instance.users[2]));
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"robot", "troll", "rando"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);
}
