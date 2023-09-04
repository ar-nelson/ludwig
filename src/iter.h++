#pragma once
#include <lmdb-safe.hh>
#include <xxhash.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <optional>
#include <string_view>
#include <assert.h>
#include <byteswap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

using std::optional, std::string_view;

namespace Ludwig {
  constexpr uint64_t ID_MAX = std::numeric_limits<uint64_t>::max();

#  if __BIG_ENDIAN__
#    define swap_bytes(x) x
#  else
#    define swap_bytes(x) bswap_64(x)
#  endif

  class Cursor {
  private:
    uint64_t data[3];
    uint8_t size;
  public:
    Cursor(const MDBInVal& v) {
      assert(v.d_mdbval.mv_size <= sizeof(data));
      assert(v.d_mdbval.mv_size > 0);
      assert(v.d_mdbval.mv_size % sizeof(uint64_t) == 0);
      memcpy(data, v.d_mdbval.mv_data, std::min(sizeof(data), v.d_mdbval.mv_size));
      size = (uint8_t)(v.d_mdbval.mv_size / sizeof(uint64_t));
    }
    Cursor(uint64_t a) : data{a}, size(1) {}
    Cursor(uint64_t a, uint64_t b) : data{swap_bytes(a), swap_bytes(b)}, size(2) {}
    Cursor(uint64_t a, uint64_t b, uint64_t c) : data{swap_bytes(a), swap_bytes(b), swap_bytes(c)}, size(3) {}
    Cursor(string_view a, uint64_t hash_seed) : data{XXH3_64bits_withSeed(a.data(), a.length(), hash_seed)}, size(1) {}

    inline auto int_field_0() const -> uint64_t {
      if (size == 1) return data[0];
      return swap_bytes(data[0]);
    }
    inline auto int_field_1() const -> uint64_t {
      assert(size >= 2);
      return swap_bytes(data[1]);
    }
    inline auto int_field_2() const -> uint64_t {
      assert(size >= 3);
      return swap_bytes(data[2]);
    }
    inline auto hash_field_0() const -> uint64_t {
      return data[0];
    }
    inline auto val() -> MDB_val {
      return { size * sizeof(uint64_t), data };
    }
    inline auto out_val() -> MDBOutVal {
      return { val() };
    }
    inline auto in_val() -> MDBInVal {
      return { out_val() };
    }

    inline friend auto operator<<(std::ostream& lhs, const Cursor& rhs) -> std::ostream& {
      switch (rhs.size) {
        case 1:
          return lhs << "Cursor(" << std::hex << rhs.int_field_0() << std::dec << ")";
        case 2:
          return lhs << "Cursor(" << std::hex << rhs.int_field_0() << "," << rhs.int_field_1() << std::dec << ")";
        case 3:
          return lhs << "Cursor(" << std::hex << rhs.int_field_0() << "," << rhs.int_field_1() << "," << rhs.int_field_2() << std::dec << ")";
        default:
          assert(false);
      }
    }
    inline auto to_string() -> std::string {
      std::ostringstream s;
      s << *this;
      return s.str();
    }
  };

# undef swap_bytes

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
        if (self.from_key) self.key = self.from_key->out_val();
        auto err = self.cur.get(self.key, self.value, self.from_key ? MDB_SET_RANGE : MDB_FIRST);
        if (err == MDB_NOTFOUND) return true;
        if (err) throw std::runtime_error("Iterator failure: " + std::string(mdb_strerror(err)));
        return gte_to_key(self);
      },
      auto (*fn_next)(DBIter<T>&) -> bool = [](DBIter<T>& self) {
        auto err = self.cur.get(self.key, self.value, MDB_NEXT);
        if (err == MDB_NOTFOUND) return true;
        if (err) throw std::runtime_error("Iterator failure: " + std::string(mdb_strerror(err)));
        return gte_to_key(self);
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
      if (fn_next(*this)) done = true;
      else n++;
    }
    auto page(uint64_t size) -> PageIter<T>;
    auto begin() -> PageIter<T>;
    inline auto end() -> IterEnd {
      return { ID_MAX };
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
    return { *this, ID_MAX };
  }
  template <typename T> static inline auto gte_to_key(DBIter<T>& i) -> bool {
    if (!i.to_key) return false;
    auto val = i.to_key->val();
    return mdb_cmp(&*i.txn, i.dbi, &i.key.d_mdbval, &val) >= 0;
  }
  template <typename T> static inline auto lte_to_key(DBIter<T>& i) -> bool {
    if (!i.to_key) return false;
    auto val = i.to_key->val();
    return mdb_cmp(&*i.txn, i.dbi, &i.key.d_mdbval, &val) <= 0;
  }

  template <typename T> static inline auto DBIterReverse(
    MDBDbi dbi,
    MDBROTransactionImpl& txn,
    optional<Cursor> from_key = {},
    optional<Cursor> to_key = {},
    auto (*fn_value)(MDBOutVal&, MDBOutVal&) -> T = [](MDBOutVal&, MDBOutVal& v) {
      return v.get<T>();
    }
  ) -> DBIter<T> {
    return DBIter<T>(dbi, txn, from_key, to_key, fn_value,
      [](DBIter<T>& self) {
        if (self.from_key) self.key = self.from_key->out_val();
        auto err = self.cur.get(self.key, self.value, self.from_key ? MDB_SET : MDB_LAST);
        if (err == MDB_NOTFOUND) {
          if (self.from_key) err = self.cur.get(self.key, self.value, MDB_PREV);
          if (err == MDB_NOTFOUND) return true;
        }
        if (err) throw std::runtime_error("Iterator failure: " + std::string(mdb_strerror(err)));
        return lte_to_key(self);
      },
      [](DBIter<T>& self) {
        auto err = self.cur.get(self.key, self.value, MDB_PREV);
        if (err == MDB_NOTFOUND) return true;
        if (err) throw std::runtime_error("Iterator failure: " + std::string(mdb_strerror(err)));
        return lte_to_key(self);
      }
    );
  }
}
