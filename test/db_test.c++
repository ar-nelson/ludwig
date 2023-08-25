#include <catch2/catch_test_macros.hpp>
#include <spdlog/spdlog.h>
#include "util.h++"
#include "../src/db.h++"
#include "../src/id.h++"

using namespace Ludwig;
using namespace flatbuffers;

struct test_init {
  test_init() {
    spdlog::set_level(spdlog::level::debug);
  }
} test_init_instance;

TEST_CASE("create DB", "[db]") {
  TempFile file;
  DB db(file.name);
}

TEST_CASE("create and get user", "[db]") {
  TempFile file;
  DB db(file.name);
  uint64_t id;
  {
    FlatBufferBuilder fbb;
    auto offset = CreateUserDirect(fbb,
      "testuser",
      "Test User",
      {},
      now_ms()
    );
    auto txn = db.open_write_txn();
    id = txn.create_user(std::move(fbb), offset);
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    auto user = txn.get_user(id);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "testuser"sv);
    REQUIRE((*user)->display_name()->string_view() == "Test User"sv);
  }
}

TEST_CASE("create and list users", "[db]") {
  TempFile file;
  DB db(file.name);
  {
    auto txn = db.open_write_txn();
    {
      FlatBufferBuilder fbb;
      txn.create_user(std::move(fbb), CreateUserDirect(fbb,
        "user1",
        "User 1",
        {},
        now_ms()
      ));
    }
    {
      FlatBufferBuilder fbb;
      txn.create_user(std::move(fbb), CreateUserDirect(fbb,
        "user2",
        "User 2",
        {},
        now_ms()
      ));
    }
    {
      FlatBufferBuilder fbb;
      txn.create_user(std::move(fbb), CreateUserDirect(fbb,
        "user3",
        "User 3",
        {},
        now_ms()
      ));
    }
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    auto iter = txn.list_users();
    REQUIRE(!iter.is_done());
    auto user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "user1"sv);
    REQUIRE((*user)->display_name()->string_view() == "User 1"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "user2"sv);
    REQUIRE((*user)->display_name()->string_view() == "User 2"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "user3"sv);
    REQUIRE((*user)->display_name()->string_view() == "User 3"sv);
    ++iter;
    REQUIRE(iter.is_done());
  }
}
