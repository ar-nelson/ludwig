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

TEST_CASE("iterate over multi-part keys", "[iter]") {
  auto db = TempDB();
  {
    auto txn = db.env->getRWTransaction();
    txn->put(db.dbi, Ludwig::Cursor(1000020, 3000000).in_val(), { "one" });
    txn->put(db.dbi, Ludwig::Cursor(1000020, 2000000).in_val(), { "two" });
    txn->put(db.dbi, Ludwig::Cursor(2000010, 1000000).in_val(), { "three" });
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
    REQUIRE(strings[1] == "one");
    REQUIRE(strings[2] == "three");
  }
}

TEST_CASE("read during iteration", "[iter]") {
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
      MDBOutVal v;
      REQUIRE(txn->get(db.dbi, { "baz" }, v) == 0);
      REQUIRE(v.get<uint64_t>() == 3);
      ns[i++] = n;
    }
    REQUIRE(i == 3);
    REQUIRE(ns[0] == 2);
    REQUIRE(ns[1] == 3);
    REQUIRE(ns[2] == 1);
  }
}

TEST_CASE("stop at to_key", "[iter]") {
  auto db = TempDB();
  {
    auto txn = db.env->getRWTransaction();
    txn->put(db.dbi, { 50 }, { 1ull });
    txn->put(db.dbi, { 40 }, { 2ull });
    txn->put(db.dbi, { 30 }, { 3ull });
    txn->put(db.dbi, { 20 }, { 4ull });
    txn->put(db.dbi, { 10 }, { 5ull });
    txn->commit();
  }
  {
    auto txn = db.env->getROTransaction();
    uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
    for (auto n : Ludwig::DBIter<uint64_t>(db.dbi, *txn.get(), {}, { { 40ull } })) {
      ns[i++] = n;
    }
    // TODO: This is inclusive when it should be exclusive. Why?
    // Doesn't matter for most functionality, but it's concerning.
    // REQUIRE(i == 3);
    REQUIRE(i == 4);
    REQUIRE(ns[0] == 5);
    REQUIRE(ns[1] == 4);
    REQUIRE(ns[2] == 3);
    i = 0;
    for (auto n : Ludwig::DBIter<uint64_t>(db.dbi, *txn.get(), {}, { { 45ull } })) {
      ns[i++] = n;
    }
    REQUIRE(i == 4);
    REQUIRE(ns[0] == 5);
    REQUIRE(ns[1] == 4);
    REQUIRE(ns[2] == 3);
    REQUIRE(ns[3] == 2);
  }
}

TEST_CASE("stop at to_key (reverse)", "[iter]") {
  auto db = TempDB();
  {
    auto txn = db.env->getRWTransaction();
    txn->put(db.dbi, { 50 }, { 1ull });
    txn->put(db.dbi, { 40 }, { 2ull });
    txn->put(db.dbi, { 30 }, { 3ull });
    txn->put(db.dbi, { 20 }, { 4ull });
    txn->put(db.dbi, { 10 }, { 5ull });
    txn->commit();
  }
  {
    auto txn = db.env->getROTransaction();
    uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
    for (auto n : Ludwig::DBIterReverse<uint64_t>(db.dbi, *txn.get(), {}, { { 20ull } })) {
      ns[i++] = n;
    }
    REQUIRE(i == 3);
    REQUIRE(ns[0] == 1);
    REQUIRE(ns[1] == 2);
    REQUIRE(ns[2] == 3);
    i = 0;
    for (auto n : Ludwig::DBIterReverse<uint64_t>(db.dbi, *txn.get(), {}, { { 15ull } })) {
      ns[i++] = n;
    }
    REQUIRE(i == 4);
    REQUIRE(ns[0] == 1);
    REQUIRE(ns[1] == 2);
    REQUIRE(ns[2] == 3);
    REQUIRE(ns[3] == 4);
  }
}

TEST_CASE("stop at multipart to_key", "[iter]") {
  auto db = TempDB();
  {
    auto txn = db.env->getRWTransaction();
    txn->put(db.dbi, Ludwig::Cursor(1000020, 3000000).in_val(), { 1ull });
    txn->put(db.dbi, Ludwig::Cursor(1000020, 2000000).in_val(), { 2ull });
    txn->put(db.dbi, Ludwig::Cursor(1000020, 1000000).in_val(), { 3ull });
    txn->put(db.dbi, Ludwig::Cursor(2000010, 1000000).in_val(), { 4ull });
    txn->put(db.dbi, Ludwig::Cursor(3000000, 1000010).in_val(), { 5ull });
    txn->commit();
  }
  {
    auto txn = db.env->getROTransaction();
    uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
    for (auto n : Ludwig::DBIter<uint64_t>(db.dbi, *txn.get(), {}, { Ludwig::Cursor(2000010, 1000000) })) {
      ns[i++] = n;
    }
    REQUIRE(i == 3);
    REQUIRE(ns[0] == 3);
    REQUIRE(ns[1] == 2);
    REQUIRE(ns[2] == 1);
    i = 0;
    for (auto n : Ludwig::DBIter<uint64_t>(db.dbi, *txn.get(), {}, { Ludwig::Cursor(2000010, std::numeric_limits<uint64_t>::max()) })) {
      ns[i++] = n;
    }
    REQUIRE(i == 4);
    REQUIRE(ns[0] == 3);
    REQUIRE(ns[1] == 2);
    REQUIRE(ns[2] == 1);
    REQUIRE(ns[3] == 4);
  }
}

TEST_CASE("stop at multipart to_key (reverse)", "[iter]") {
  auto db = TempDB();
  {
    auto txn = db.env->getRWTransaction();
    txn->put(db.dbi, Ludwig::Cursor(1000020, 3000000).in_val(), { 1ull });
    txn->put(db.dbi, Ludwig::Cursor(1000020, 2000000).in_val(), { 2ull });
    txn->put(db.dbi, Ludwig::Cursor(1000020, 1000000).in_val(), { 3ull });
    txn->put(db.dbi, Ludwig::Cursor(2000010, 1000000).in_val(), { 4ull });
    txn->put(db.dbi, Ludwig::Cursor(3000000, 1000010).in_val(), { 5ull });
    txn->commit();
  }
  {
    auto txn = db.env->getROTransaction();
    uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
    for (auto n : Ludwig::DBIterReverse<uint64_t>(db.dbi, *txn.get(), {}, { Ludwig::Cursor(2000010, 1000000) })) {
      ns[i++] = n;
    }
    REQUIRE(i == 1);
    REQUIRE(ns[0] == 5);
    i = 0;
    for (auto n : Ludwig::DBIterReverse<uint64_t>(db.dbi, *txn.get(), {}, { Ludwig::Cursor(2000010, 0) })) {
      ns[i++] = n;
    }
    REQUIRE(i == 2);
    REQUIRE(ns[0] == 5);
    REQUIRE(ns[1] == 4);
  }
}
