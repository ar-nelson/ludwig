#pragma once
#include <lmdb-safe.hh>
#include <xxhash.h>
#include <optional>
#include <string_view>
#include <assert.h>
#include <byteswap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

using std::optional, std::string_view;

namespace Ludwig {
  class Cursor {
  private:
    uint64_t data[3];
  public:
    MDBInVal val = { 0 };

    Cursor(const MDBInVal& v) {
      if (v.d_mdbval.mv_size == sizeof(uint64_t)) {
        val = MDBInVal(*reinterpret_cast<uint64_t*>(v.d_mdbval.mv_data));
      } else {
        assert(v.d_mdbval.mv_size <= sizeof(data));
        memcpy(data, v.d_mdbval.mv_data, std::min(sizeof(data), v.d_mdbval.mv_size));
        val = MDBInVal({ .d_mdbval = { v.d_mdbval.mv_size, data } });
      }
    }
    Cursor(uint64_t a) : val(a) {}
    Cursor(uint64_t a, uint64_t b) {
#     if __BIG_ENDIAN__
      data[0] = a;
      data[1] = b;
#     else
      data[0] = bswap_64(a);
      data[1] = bswap_64(b);
#     endif
      val = MDBInVal({ .d_mdbval = { sizeof(uint64_t) * 2, data } });
    }
    Cursor(uint64_t a, uint64_t b, uint64_t c) {
#     if __BIG_ENDIAN__
      data[0] = a;
      data[1] = b;
      data[2] = c;
#     else
      data[0] = bswap_64(a);
      data[1] = bswap_64(b);
      data[2] = bswap_64(c);
#     endif
      val = MDBInVal({ .d_mdbval = { sizeof(uint64_t) * 3, data } });
    }
    Cursor(string_view a, uint64_t hash_seed) : val(XXH3_64bits_withSeed(a.data(), a.length(), hash_seed)) {}
    Cursor(string_view a, uint64_t b, uint64_t hash_seed) {
      data[0] = XXH3_64bits_withSeed(a.data(), a.length(), hash_seed);
#     if __BIG_ENDIAN__
      data[1] = b;
#     else
      data[1] = bswap_64(b);
#     endif
      val = MDBInVal({ .d_mdbval = { sizeof(uint64_t) * 2, data } });
    }
    Cursor(uint64_t a, string_view b, uint64_t hash_seed) {
#     if __BIG_ENDIAN__
      data[0] = a;
#     else
      data[0] = bswap_64(a);
#     endif
      data[1] = XXH3_64bits_withSeed(b.data(), b.length(), hash_seed);
      val = { { sizeof(uint64_t) * 2, data } };
    }
    Cursor(string_view a, string_view b, uint64_t hash_seed) {
      data[0] = XXH3_64bits_withSeed(a.data(), a.length(), hash_seed);
      data[1] = XXH3_64bits_withSeed(b.data(), b.length(), hash_seed);
      val = { { sizeof(uint64_t) * 2, data } };
    }

    inline auto int_field_0() -> uint64_t {
      if (val.d_mdbval.mv_size == sizeof(uint64_t)) {
        return *reinterpret_cast<uint64_t*>(val.d_mdbval.mv_data);
      }
#     if __BIG_ENDIAN__
      return data[0];
#     else
      return bswap_64(data[0]);
#     endif
    }
    inline auto int_field_1() -> uint64_t {
#     if __BIG_ENDIAN__
      return data[1];
#     else
      return bswap_64(data[1]);
#     endif
    }
    inline auto int_field_2() -> uint64_t {
#     if __BIG_ENDIAN__
      return data[2];
#     else
      return bswap_64(data[2]);
#     endif
    }
    inline auto hash_field_0() -> uint64_t {
      if (val.d_mdbval.mv_size == sizeof(uint64_t)) {
        return *reinterpret_cast<uint64_t*>(val.d_mdbval.mv_data);
      }
      return data[0];
    }
    inline auto hash_field_1() -> uint64_t {
      return data[1];
    }
    inline auto hash_field_2() -> uint64_t {
      return data[2];
    }
  };

  template <typename T> struct DBIter;
  template <typename T> struct PageIter;

  struct IterEnd {
    uint64_t n;
  };

  template <typename T> static auto gte_to_key(DBIter<T>& iter) -> bool;
  template <typename T> static auto lte_to_key(DBIter<T>& iter) -> bool;

  template <typename T> struct DBIter {
    MDBDbi dbi;
    MDBROTransactionImpl& txn;
    MDBROCursor cur;
    uint64_t n = 0;
    bool done = false;
    optional<Cursor> from_key, to_key;
    MDBOutVal key, value;
    auto (*fn_value)(MDBOutVal&, MDBOutVal&) -> T;
    auto (*fn_first)(DBIter<T>&) -> bool;
    auto (*fn_next)(DBIter<T>&) -> bool;

    DBIter(
      MDBDbi dbi,
      MDBROTransactionImpl& txn,
      optional<Cursor> from_key = {},
      optional<Cursor> to_key = {},
      auto (*fn_value)(MDBOutVal&, MDBOutVal&) -> T = [](MDBOutVal&, MDBOutVal& v) {
        return v.get<T>();
      },
      auto (*fn_first)(DBIter<T>&) -> bool = [](DBIter<T>& self) {
        return (self.from_key
          ? self.cur.find(self.from_key->val, self.key, self.value)
          : self.cur.get(self.key, self.value, MDB_FIRST)
        ) || gte_to_key(self);
      },
      auto (*fn_next)(DBIter<T>&) -> bool = [](DBIter<T>& self) {
        return self.cur.get(self.key, self.value, MDB_NEXT) || gte_to_key(self);
      }
    ) : dbi(dbi), txn(txn), from_key(from_key), to_key(to_key),
        fn_value(fn_value), fn_first(fn_first), fn_next(fn_next) {
      cur = txn.getROCursor(dbi);
      done = fn_first(*this);
    }

    inline auto get_cursor() const -> optional<Cursor> {
      if (done) return {};
      return { Cursor(key) };
    }
    inline auto is_done() const -> bool {
      return done;
    }
    inline auto operator*() -> T {
      return fn_value(key, value);
    }
    inline auto operator++() -> void {
      if (done) return;
      else if (fn_next(*this)) done = true;
      else n++;
    }
    auto page(uint64_t size) -> PageIter<T>;
    auto begin() -> PageIter<T>;
    inline auto end() -> IterEnd {
      return { std::numeric_limits<uint64_t>::max() };
    }
  };

  template <typename T> struct PageIter {
    DBIter<T>& iter;
    uint64_t limit;

    inline auto operator*() -> T {
      return *iter;
    }
    inline auto operator++() -> void {
      ++iter;
    }
    inline auto begin() -> PageIter<T> {
      return *this;
    }
    inline auto end() -> IterEnd {
      return { limit };
    }
    inline friend auto operator!=(const PageIter<T> &lhs, IterEnd rhs) -> bool {
      return !lhs.iter.done && lhs.iter.n < rhs.n;
    }
  };

  template <typename T> inline auto DBIter<T>::page(uint64_t size) -> PageIter<T> {
    return { *this, n + size };
  }
  template <typename T> inline auto DBIter<T>::begin() -> PageIter<T> {
    return { *this, std::numeric_limits<uint64_t>::max() };
  }
  template <typename T> static inline auto gte_to_key(DBIter<T>& i) -> bool {
    return i.to_key && mdb_cmp(&*i.txn, i.dbi, &i.key.d_mdbval, &i.to_key->val.d_mdbval) >= 0;
  }
  template <typename T> static inline auto lte_to_key(DBIter<T>& i) -> bool {
    return i.to_key && mdb_cmp(&*i.txn, i.dbi, &i.key.d_mdbval, &i.to_key->val.d_mdbval) <= 0;
  }

  template <typename T> static inline auto DBIterReverse(
    MDBDbi dbi,
    MDBROTransactionImpl& txn,
    auto (*fn_value)(MDBOutVal& k, MDBOutVal& v) -> T,
    optional<Cursor> from_key = {},
    optional<Cursor> to_key = {}
  ) -> DBIter<T> {
    return DBIter(dbi, txn, fn_value, from_key, to_key,
      [](DBIter<T>& self) {
        return (self.from_key
          ? self.cur.find(self.from_key->val, self.key, self.value)
          : self.cur.get(self.key, self.value, MDB_LAST)
        ) || lte_to_key(self);
      },
      [](DBIter<T>& self) {
        return self.cur.get(self.key, self.value, MDB_PREV) || lte_to_key(self);
      }
    );
  }
}
