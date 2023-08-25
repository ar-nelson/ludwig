#include <catch2/catch_test_macros.hpp>
#include "util.h++"
#include "../src/iter.h++"

TEST_CASE("read and write from test DB", "[iter]") {
  auto db = TempDB();
  MDBInVal k("foo");
  {
    auto txn = db.env->getRWTransaction();
    MDBInVal v("bar");
    txn->put(db.dbi, k, v);
    txn->commit();
  }
  {
    auto txn = db.env->getROTransaction();
    MDBOutVal v;
    auto res = txn->get(db.dbi, k, v);
    REQUIRE(res == 0);
    REQUIRE(v.get<std::string_view>() == "bar"sv);
  }
}

TEST_CASE("iterate over uint64s", "[iter]") {
  auto db = TempDB();
  {
    auto txn = db.env->getRWTransaction();
    txn->put(db.dbi, { "foo" }, { 1ull });
    txn->put(db.dbi, { "bar" }, { 2ull });
    txn->put(db.dbi, { "baz" }, { 3ull });
    txn->commit();
  }
  {
    auto txn = db.env->getROTransaction();
    uint64_t i = 0, ns[] = { 0, 0, 0 };
    for (auto n : Ludwig::DBIter<uint64_t>(db.dbi, *txn.get())) {
      ns[i++] = n;
    }
    REQUIRE(i == 3);
    REQUIRE(ns[0] == 2);
    REQUIRE(ns[1] == 3);
    REQUIRE(ns[2] == 1);
  }
}

TEST_CASE("iterate over strings", "[iter]") {
  auto db = TempDB();
  {
    auto txn = db.env->getRWTransaction();
    txn->put(db.dbi, { "foo" }, { "one" });
    txn->put(db.dbi, { "bar" }, { "two" });
    txn->put(db.dbi, { "baz" }, { "three" });
    txn->commit();
  }
  {
    auto txn = db.env->getROTransaction();
    std::vector<std::string> strings;
    for (auto s : Ludwig::DBIter<std::string>(db.dbi, *txn.get())) {
      strings.push_back(s);
    }
    REQUIRE(strings.size() == 3);
    REQUIRE(strings[0] == "two");
    REQUIRE(strings[1] == "three");
    REQUIRE(strings[2] == "one");
  }
}
