#pragma once
#include "util/common.h++"
#include <lmdb.h>
#include <xxhash.h>
#include <sstream>
#include <assert.h>
#include <byteswap.h>

namespace Ludwig {
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
    Cursor(const MDB_val& v) {
      assert(v.mv_size <= sizeof(data));
      assert(v.mv_size > 0);
      assert(v.mv_size % sizeof(uint64_t) == 0);
      memcpy(data, v.mv_data, std::min(sizeof(data), v.mv_size));
      size = (uint8_t)(v.mv_size / sizeof(uint64_t));
    }
    Cursor(uint64_t a) : data{a}, size(1) {}
    Cursor(uint64_t a, uint64_t b) : data{swap_bytes(a), swap_bytes(b)}, size(2) {}
    Cursor(uint64_t a, uint64_t b, uint64_t c) : data{swap_bytes(a), swap_bytes(b), swap_bytes(c)}, size(3) {}
    Cursor(std::string_view a, uint64_t hash_seed) : data{XXH3_64bits_withSeed(a.data(), a.length(), hash_seed)}, size(1) {}

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

  template <typename T> static auto gte_to_key(DBIter<T>& iter) noexcept -> bool;
  template <typename T> static auto lte_to_key(DBIter<T>& iter) noexcept -> bool;

  template <class T, typename std::enable_if<std::is_arithmetic<T>::value, T>::type* = nullptr>
  inline auto val_as(const MDB_val& v) noexcept -> T {
    T ret;
    if (v.mv_size != sizeof(T)) {
      spdlog::error("DATABASE CORRUPTION: DB entry is wrong size (expected {}B, got {}B); returning 0", sizeof(T), v.mv_size);
      return 0;
    }
    memcpy(&ret, v.mv_data, sizeof(T));
    return ret;
  }

  template <class T, typename std::enable_if<std::is_pointer<T>::value,T>::type* = nullptr>
  inline auto val_as(const MDB_val& v) noexcept -> T {
    return static_cast<T>(v.mv_data);
  }

  template <typename T> struct DBIter {
    MDB_dbi dbi;
    MDB_txn* txn = nullptr;
    MDB_cursor* cur = nullptr;
    uint64_t n = 0;
    bool done = false;
    std::optional<Cursor> from_key, to_key;
    MDB_val key, value;
    auto (*fn_value)(MDB_val&, MDB_val&) -> T;
    auto (*fn_first)(DBIter<T>&) -> bool;
    auto (*fn_next)(DBIter<T>&) -> bool;

    bool failed = false;
    DBIter(
      MDB_dbi dbi,
      MDB_txn* txn,
      std::optional<Cursor> from_key = {},
      std::optional<Cursor> to_key = {},
      auto (*fn_value)(MDB_val&, MDB_val&) -> T = [](MDB_val&, MDB_val& v) noexcept {
        return val_as<T>(v);
      },
      auto (*fn_first)(DBIter<T>&) -> bool = [](DBIter<T>& self) noexcept {
        if (self.from_key) self.key = self.from_key->val();
        if (auto err = mdb_cursor_get(self.cur, &self.key, &self.value, self.from_key ? MDB_SET_RANGE : MDB_FIRST)) {
          if (err != MDB_NOTFOUND) {
            self.failed = true;
            spdlog::error("Database error in iterator: {}", mdb_strerror(err));
          }
          return true;
        }
        return gte_to_key(self);
      },
      auto (*fn_next)(DBIter<T>&) -> bool = [](DBIter<T>& self) noexcept {
        if (auto err = mdb_cursor_get(self.cur, &self.key, &self.value, MDB_NEXT)) {
          if (err != MDB_NOTFOUND) {
            self.failed = true;
            spdlog::error("Database error in iterator: {}", mdb_strerror(err));
          }
          return true;
        }
        return gte_to_key(self);
      }
    ) : dbi(dbi), txn(txn), from_key(from_key), to_key(to_key),
        fn_value(fn_value), fn_first(fn_first), fn_next(fn_next) {
      const auto err = mdb_cursor_open(txn, dbi, &cur);
      if (err) {
        spdlog::error("Failed to create iterator: {}", mdb_strerror(err));
        failed = true;
        done = true;
      } else {
        done = fn_first(*this);
      }
    }
    DBIter(const DBIter&) = delete;
    auto operator=(const DBIter&) = delete;
    DBIter(DBIter&& from) {
      memcpy(this, &from, sizeof(DBIter));
      from.cur = nullptr;
    };
    DBIter& operator=(DBIter&& from) noexcept {
      memcpy(this, &from, sizeof(DBIter));
      from.cur = nullptr;
    }
    ~DBIter() {
      if (cur != nullptr) mdb_cursor_close(cur);
    }

    inline auto get_cursor() const noexcept -> std::optional<Cursor> {
      if (done) return {};
      return { Cursor(key) };
    }
    inline auto is_done() const noexcept -> bool {
      return done;
    }
    inline auto operator*() noexcept -> T {
      assert(!done);
      return fn_value(key, value);
    }
    inline auto operator++() noexcept -> void {
      if (done) return;
      if (fn_next(*this)) done = true;
      else n++;
    }
    auto page(uint64_t size) noexcept -> PageIter<T>;
    auto begin() noexcept -> PageIter<T>;
    inline auto end() noexcept -> IterEnd {
      return { ID_MAX };
    }
  };

  template <typename T> struct PageIter {
    DBIter<T>& iter;
    uint64_t limit;

    inline auto operator*() noexcept -> T {
      return *iter;
    }
    inline auto operator++() noexcept -> void {
      ++iter;
    }
    inline auto begin() noexcept -> PageIter<T> {
      return *this;
    }
    inline auto end() noexcept -> IterEnd {
      return { limit };
    }
    inline friend auto operator!=(const PageIter<T> &lhs, IterEnd rhs) noexcept -> bool {
      return !lhs.iter.done && lhs.iter.n < rhs.n;
    }
  };

  template <typename T> inline auto DBIter<T>::page(uint64_t size) noexcept -> PageIter<T> {
    return { *this, n + size };
  }
  template <typename T> inline auto DBIter<T>::begin() noexcept -> PageIter<T> {
    return { *this, ID_MAX };
  }
  template <typename T> static inline auto gte_to_key(DBIter<T>& i) noexcept -> bool {
    if (!i.to_key) return false;
    auto val = i.to_key->val();
    return mdb_cmp(i.txn, i.dbi, &i.key, &val) >= 0;
  }
  template <typename T> static inline auto lte_to_key(DBIter<T>& i) noexcept -> bool {
    if (!i.to_key) return false;
    auto val = i.to_key->val();
    return mdb_cmp(i.txn, i.dbi, &i.key, &val) <= 0;
  }

  template <typename T> static inline auto DBIterReverse(
    MDB_dbi dbi,
    MDB_txn* txn,
    std::optional<Cursor> from_key = {},
    std::optional<Cursor> to_key = {},
    auto (*fn_value)(MDB_val&, MDB_val&) -> T = [](MDB_val&, MDB_val& v) noexcept {
      return val_as<T>(v);
    }
  ) -> DBIter<T> {
    return DBIter<T>(dbi, txn, from_key, to_key, fn_value,
      [](DBIter<T>& self) noexcept {
        if (self.from_key) self.key = self.from_key->val();
        auto err = mdb_cursor_get(self.cur, &self.key, &self.value, self.from_key ? MDB_SET : MDB_LAST);
        if (err == MDB_NOTFOUND) {
          if (self.from_key) err = mdb_cursor_get(self.cur, &self.key, &self.value, MDB_PREV);
        }
        if (err) {
          if (err != MDB_NOTFOUND) {
            self.failed = true;
            spdlog::error("Database error in iterator: {}", mdb_strerror(err));
          }
          return true;
        }
        return lte_to_key(self);
      },
      [](DBIter<T>& self) noexcept {
        if (auto err = mdb_cursor_get(self.cur, &self.key, &self.value, MDB_PREV)) {
          if (err != MDB_NOTFOUND) {
            self.failed = true;
            spdlog::error("Database error in iterator: {}", mdb_strerror(err));
          }
          return true;
        }
        return lte_to_key(self);
      }
    );
  }
}
