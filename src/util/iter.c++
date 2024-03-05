#include "iter.h++"

using std::optional, std::pair;

namespace Ludwig {
  DBIter::DBIter(
    MDB_dbi dbi,
    MDB_txn* txn,
    Dir dir,
    optional<MDB_val> from_key,
    optional<Cursor> to_key
  ) : dbi(dbi), txn(txn), dir(dir), to_key(to_key) {
    int err;
    if ((err = mdb_cursor_open(txn, dbi, &cur))) {
      spdlog::error("Failed to create iterator: {}", mdb_strerror(err));
      done = failed = true;
    } else {
      if (from_key) key = *from_key;
      err = mdb_cursor_get(cur, &key, &value, dir == Dir::Asc
        ? (from_key ? MDB_SET_RANGE : MDB_FIRST)
        : (from_key ? MDB_SET : MDB_LAST)
      );
      if (err == MDB_NOTFOUND && dir == Dir::Desc && from_key) {
        err = mdb_cursor_get(cur, &key, &value, MDB_PREV_NODUP);
      }
      if (err) {
        if (err != MDB_NOTFOUND) {
          failed = true;
          spdlog::error("Database error in iterator: {}", mdb_strerror(err));
        }
        done = true;
      } else done = reached_to_key();
    }
  }

  DBIter::DBIter(
    MDB_dbi dbi,
    MDB_txn* txn,
    Dir dir,
    pair<Cursor, uint64_t> from_kv,
    optional<Cursor> to_key
  ) : dbi(dbi), txn(txn), dir(dir), to_key(to_key), key(from_kv.first.val()) {
    int err;
    if ((err = mdb_cursor_open(txn, dbi, &cur))) {
      spdlog::error("Failed to create iterator: {}", mdb_strerror(err));
      done = failed = true;
    } else {
      Cursor value_cur(from_kv.second);
      value = value_cur.val();
      err = mdb_cursor_get(cur, &key, &value, dir == Dir::Asc ? MDB_GET_BOTH_RANGE : MDB_GET_BOTH);
      if (err == MDB_NOTFOUND) {
        key = from_kv.first.val();
        switch (dir) {
        case Dir::Asc:
          if (!(err = mdb_cursor_get(cur, &key, &value, MDB_SET_RANGE))) {
            auto key_ref = from_kv.first.val(), value_ref = value_cur.val();
            while (!err && !mdb_cmp(txn, dbi, &key, &key_ref) && mdb_cmp(txn, dbi, &value, &value_ref) < 0) {
              err = mdb_cursor_get(cur, &key, &value, MDB_NEXT);
            }
          }
          break;
        case Dir::Desc:
          if (!(err = mdb_cursor_get(cur, &key, &value, MDB_SET))) {
            err = mdb_cursor_get(cur, &key, &value, MDB_LAST_DUP);
            auto key_ref = from_kv.first.val(), value_ref = value_cur.val();
            while (!err && !mdb_cmp(txn, dbi, &key, &key_ref) && mdb_cmp(txn, dbi, &value, &value_ref) > 0) {
              err = mdb_cursor_get(cur, &key, &value, MDB_PREV);
            }
          } else if (err == MDB_NOTFOUND) {
            err = mdb_cursor_get(cur, &key, &value, MDB_PREV_NODUP);
          }
          break;
        }
      }
      if (err) {
        if (err != MDB_NOTFOUND) {
          failed = true;
          spdlog::error("Database error in iterator: {}", mdb_strerror(err));
        }
        done = true;
      } else done = reached_to_key();
    }
  }

  auto DBIter::operator++() noexcept -> DBIter& {
    if (done) {
      return *this;
    } else if (const auto err = mdb_cursor_get(cur, &key, &value, dir == Dir::Asc ? MDB_NEXT : MDB_PREV)) {
      if (err != MDB_NOTFOUND) {
        failed = true;
        spdlog::error("Database error in iterator: {}", mdb_strerror(err));
      }
      done = true;
    } else if (reached_to_key()) {
      done = true;
    } else {
      n++;
    }
    return *this;
  }
}
