#include "test_common.h++"
#include "util/iter.h++"

static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, string_view v) -> void {
  MDB_val kval { k.length(), const_cast<char*>(k.data()) };
  MDB_val vval { v.length(), const_cast<char*>(v.data()) };
  mdb_put(txn, dbi, &kval, &vval, 0);
}

static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, string_view k, uint64_t v) -> void {
  MDB_val kval { k.length(), const_cast<char*>(k.data()) };
  MDB_val vval { sizeof(uint64_t), &v };
  mdb_put(txn, dbi, &kval, &vval, 0);
}

static inline auto db_put(MDB_txn* txn, MDB_dbi dbi, Cursor k, uint64_t v) -> void {
  MDB_val kval = k.val();
  MDB_val vval { sizeof(uint64_t), &v };
  mdb_put(txn, dbi, &kval, &vval, 0);
}

TEST_CASE("read and write from test DB", "[iter]") {
  auto db = TempDB();
  MDB_val k{ 3, (void*)"foo" };
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  MDB_val v{ 3, (void*)"bar" };
  REQUIRE(!mdb_put(txn, db.dbi, &k, &v, 0));
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  REQUIRE(!mdb_get(txn, db.dbi, &k, &v));
  REQUIRE(string_view(static_cast<const char*>(v.mv_data), v.mv_size) == "bar"sv);
  mdb_txn_abort(txn);
}

TEST_CASE("iterate over uint64s", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, "foo", 1);
  db_put(txn, db.dbi, "bar", 2);
  db_put(txn, db.dbi, "baz", 3);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  uint64_t i = 0, ns[] = { 0, 0, 0 };
  for (auto n : DBIter(db.dbi, txn, Dir::Asc)) {
    ns[i++] = n;
  }
  REQUIRE(i == 3);
  REQUIRE(ns[0] == 2);
  REQUIRE(ns[1] == 3);
  REQUIRE(ns[2] == 1);
  mdb_txn_abort(txn);
}

TEST_CASE("iterate over strings", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, "foo", 1);
  db_put(txn, db.dbi, "bar", 2);
  db_put(txn, db.dbi, "baz", 3);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  vector<uint64_t> ints;
  for (auto i : DBIter(db.dbi, txn, Dir::Asc)) {
    ints.push_back(i);
  }
  REQUIRE(ints.size() == 3);
  REQUIRE(ints[0] == 2);
  REQUIRE(ints[1] == 3);
  REQUIRE(ints[2] == 1);
  mdb_txn_abort(txn);
}

TEST_CASE("iterate over multi-part keys", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, Cursor(1000020, 3000000), 1);
  db_put(txn, db.dbi, Cursor(1000020, 2000000), 2);
  db_put(txn, db.dbi, Cursor(2000010, 1000000), 3);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  vector<uint64_t> ints;
  for (auto i : DBIter(db.dbi, txn, Dir::Asc)) {
    ints.push_back(i);
  }
  REQUIRE(ints.size() == 3);
  REQUIRE(ints[0] == 2);
  REQUIRE(ints[1] == 1);
  REQUIRE(ints[2] == 3);
  mdb_txn_abort(txn);
}

TEST_CASE("read during iteration", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, "foo", 1);
  db_put(txn, db.dbi, "bar", 2);
  db_put(txn, db.dbi, "baz", 3);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  uint64_t i = 0, ns[] = { 0, 0, 0 };
  for (auto n : DBIter(db.dbi, txn, Dir::Asc)) {
    MDB_val k{ 3, (void*)"baz" }, v;
    REQUIRE(!mdb_get(txn, db.dbi, &k, &v));
    REQUIRE(val_as<uint64_t>(v) == 3);
    ns[i++] = n;
  }
  REQUIRE(i == 3);
  REQUIRE(ns[0] == 2);
  REQUIRE(ns[1] == 3);
  REQUIRE(ns[2] == 1);
  mdb_txn_abort(txn);
}

TEST_CASE("stop at to_key", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, 50, 1);
  db_put(txn, db.dbi, 40, 2);
  db_put(txn, db.dbi, 30, 3);
  db_put(txn, db.dbi, 20, 4);
  db_put(txn, db.dbi, 10, 5);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, optional<MDB_val>(), { { 40ull } })) {
    ns[i++] = n;
  }
  REQUIRE(i == 3);
  REQUIRE(ns[0] == 5);
  REQUIRE(ns[1] == 4);
  REQUIRE(ns[2] == 3);
  i = 0;
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, optional<MDB_val>(), { { 45ull } })) {
    ns[i++] = n;
  }
  REQUIRE(i == 4);
  REQUIRE(ns[0] == 5);
  REQUIRE(ns[1] == 4);
  REQUIRE(ns[2] == 3);
  REQUIRE(ns[3] == 2);
  mdb_txn_abort(txn);
}

TEST_CASE("stop at to_key (reverse)", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, 50, 1);
  db_put(txn, db.dbi, 40, 2);
  db_put(txn, db.dbi, 30, 3);
  db_put(txn, db.dbi, 20, 4);
  db_put(txn, db.dbi, 10, 5);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, optional<MDB_val>(), { { 20ull } })) {
    ns[i++] = n;
  }
  REQUIRE(i == 3);
  REQUIRE(ns[0] == 1);
  REQUIRE(ns[1] == 2);
  REQUIRE(ns[2] == 3);
  i = 0;
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, optional<MDB_val>(), { { 15ull } })) {
    ns[i++] = n;
  }
  REQUIRE(i == 4);
  REQUIRE(ns[0] == 1);
  REQUIRE(ns[1] == 2);
  REQUIRE(ns[2] == 3);
  REQUIRE(ns[3] == 4);
  mdb_txn_abort(txn);
}

TEST_CASE("stop at multipart to_key", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, Cursor(1000020, 3000000), 1);
  db_put(txn, db.dbi, Cursor(1000020, 2000000), 2);
  db_put(txn, db.dbi, Cursor(1000020, 1000000), 3);
  db_put(txn, db.dbi, Cursor(2000010, 1000000), 4);
  db_put(txn, db.dbi, Cursor(3000000, 1000010), 5);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, optional<MDB_val>(), { Cursor(2000010, 1000000) })) {
    ns[i++] = n;
  }
  REQUIRE(i == 3);
  REQUIRE(ns[0] == 3);
  REQUIRE(ns[1] == 2);
  REQUIRE(ns[2] == 1);
  i = 0;
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, optional<MDB_val>(), { Cursor(2000010, ID_MAX) })) {
    ns[i++] = n;
  }
  REQUIRE(i == 4);
  REQUIRE(ns[0] == 3);
  REQUIRE(ns[1] == 2);
  REQUIRE(ns[2] == 1);
  REQUIRE(ns[3] == 4);
  mdb_txn_abort(txn);
}

TEST_CASE("stop at multipart to_key (reverse)", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, Cursor(1000020, 3000000), 1);
  db_put(txn, db.dbi, Cursor(1000020, 2000000), 2);
  db_put(txn, db.dbi, Cursor(1000020, 1000000), 3);
  db_put(txn, db.dbi, Cursor(2000010, 1000000), 4);
  db_put(txn, db.dbi, Cursor(3000000, 1000010), 5);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  uint64_t i = 0, ns[] = { 0, 0, 0, 0, 0 };
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, optional<MDB_val>(), { Cursor(2000010, 1000000) })) {
    ns[i++] = n;
  }
  REQUIRE(i == 1);
  REQUIRE(ns[0] == 5);
  i = 0;
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, optional<MDB_val>(), { Cursor(2000010, 0) })) {
    ns[i++] = n;
  }
  REQUIRE(i == 2);
  REQUIRE(ns[0] == 5);
  REQUIRE(ns[1] == 4);
  mdb_txn_abort(txn);
}

TEST_CASE("start at key and value", "[iter]") {
  auto db = TempDB();
  MDB_txn* txn;

  REQUIRE(!mdb_txn_begin(db.env, nullptr, 0, &txn));
  db_put(txn, db.dbi, Cursor(1), 101);
  db_put(txn, db.dbi, Cursor(1), 103);
  db_put(txn, db.dbi, Cursor(3), 105);
  db_put(txn, db.dbi, Cursor(3), 107);
  db_put(txn, db.dbi, Cursor(3), 109);
  REQUIRE(!mdb_txn_commit(txn));

  REQUIRE(!mdb_txn_begin(db.env, nullptr, MDB_RDONLY, &txn));
  vector<uint64_t> xs;
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, pair(Cursor(0), 0))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{101, 103, 105, 107, 109});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, pair(Cursor(ID_MAX), ID_MAX))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{109, 107, 105, 103, 101});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, pair(Cursor(1), 103))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{103, 105, 107, 109});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, pair(Cursor(3), 107))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{107, 105, 103, 101});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, pair(Cursor(1), 102))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{103, 105, 107, 109});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, pair(Cursor(1), 200))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{105, 107, 109});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Asc, pair(Cursor(2), 100))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{105, 107, 109});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, pair(Cursor(3), 106))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{105, 103, 101});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, pair(Cursor(3), 200))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{109, 107, 105, 103, 101});
  xs.clear();
  for (auto n : DBIter(db.dbi, txn, Dir::Desc, pair(Cursor(2), 100))) xs.push_back(n);
  REQUIRE(xs == vector<uint64_t>{103, 101});
  mdb_txn_abort(txn);
}
