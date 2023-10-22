#include "test_common.h++"
#include "services/db.h++"
#include "controllers/instance.h++"
#include <sstream>
#include <iomanip>

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset;

#define HOUR 3600
#define DAY (HOUR * 24)

template <typename T> inline auto build(FlatBufferBuilder& fbb, Offset<T> t) -> FlatBufferBuilder& {
  fbb.Finish(t);
  return fbb;
}

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
    users[0] = txn.create_user(build(fbb, CreateUserDirect(fbb,
      "admin",
      "Admin User",
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      {},
      epoch
    )));
    fbb.Clear();
    {
      auto email = fbb.CreateString("admin@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_admin(true);
      lu.add_approved(true);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[0], fbb);
    fbb.Clear();
    users[1] = txn.create_user(build(fbb, CreateUserDirect(fbb,
      "rando",
      "Some Local Rando",
      "This is *my* bio",
      "This is <em>my</em> bio",
      nullptr,
      nullptr,
      {},
      epoch + HOUR,
      epoch + DAY * 2
    )));
    fbb.Clear();
    {
      auto email = fbb.CreateString("rando@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(true);
      lu.add_show_bot_accounts(false);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[1], fbb);
    fbb.Clear();
    users[2] = txn.create_user(build(fbb, CreateUserDirect(fbb,
      "troll",
      "Banned Troll",
      "Problem?",
      "Problem?",
      nullptr,
      nullptr,
      {},
      epoch + DAY,
      {},
      {},
      nullptr,
      nullptr,
      false,
      ModState::Removed,
      "begone"
    )));
    fbb.Clear();
    {
      auto email = fbb.CreateString("troll@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(true);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[2], fbb);
    txn.set_hide_user(users[2], users[0], true);
    fbb.Clear();
    users[3] = txn.create_user(build(fbb, CreateUserDirect(fbb,
      "robot",
      "Mr. Roboto",
      "domo",
      "domo",
      nullptr,
      nullptr,
      {},
      epoch + DAY + HOUR * 2,
      {},
      {},
      nullptr,
      nullptr,
      true
    )));
    fbb.Clear();
    {
      auto email = fbb.CreateString("robot@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(true);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[3], fbb);
    fbb.Clear();
    users[4] = txn.create_user(build(fbb, CreateUserDirect(fbb,
      "visitor@federated.test",
      "Visitor from Elsewhere",
      nullptr,
      nullptr,
      "https://federated.test/ap/user/visitor",
      "https://federated.test/ap/user/visitor/inbox",
      1,
      epoch + DAY + HOUR
    )));
    fbb.Clear();
    users[5] = txn.create_user(build(fbb, CreateUserDirect(fbb,
      "unapproved",
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      {},
      epoch + DAY * 5
    )));
    fbb.Clear();
    {
      auto email = fbb.CreateString("unapproved@ludwig.test");
      LocalUserBuilder lu(fbb);
      lu.add_email(email);
      lu.add_approved(false);
      fbb.Finish(lu.Finish());
    }
    txn.set_local_user(users[5], fbb);
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
      fbb.Clear();
      txn.create_user(build(fbb, CreateUserDirect(fbb,
        fmt::format("filler{}@federated.test", i).c_str(),
        nullptr,
        nullptr,
        nullptr,
        "https://federated.test/ap/user/visitor",
        "https://federated.test/ap/user/visitor/inbox",
        1,
        epoch + DAY * 6 + i
      )));
    }
    txn.commit();
    controller = make_shared<InstanceController>(db, nullptr);
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
  page = instance->list_users(txn, UserSortType::New, true, instance->local_user_detail(txn, instance.users[0]));
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"unapproved", "robot", "troll", "rando", "admin"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);

  // New, logged in as rando (excludes bots), local only
  page = instance->list_users(txn, UserSortType::New, true, instance->local_user_detail(txn, instance.users[1]));
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"rando", "admin"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);

  // New, logged in as troll (includes self, hides admin), local only
  page = instance->list_users(txn, UserSortType::New, true, instance->local_user_detail(txn, instance.users[2]));
  REQUIRE(page.entries.size() > 0);
  vec.clear();
  for (auto& i : page.entries) vec.emplace_back(i.user().name()->str());
  REQUIRE(vec == vector<string>{"robot", "troll", "rando"});
  REQUIRE(page.is_first);
  REQUIRE(!page.next);
}
