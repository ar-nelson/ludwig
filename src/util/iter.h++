#pragma once
#include "util/common.h++"
#include <lmdb.h>
#include <sstream>
#include <assert.h>
#include <byteswap.h>

namespace Ludwig {
#  if __BIG_ENDIAN__
#    define swap_bytes(x) x
#  else
#    define swap_bytes(x) bswap_64(x)
#  endif

  class Cursor final {
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
    inline auto val() -> MDB_val {
      return { size * sizeof(uint64_t), data };
    }

    inline auto to_string() -> std::string {
      switch (size) {
        case 1:
          return fmt::format("Cursor({:x})", int_field_0());
        case 2:
          return fmt::format("Cursor({:x},{:x})", int_field_0(), int_field_1());
        case 3:
          return fmt::format("Cursor({:x},{:x},{:x})", int_field_0(), int_field_1(), int_field_2());
        default:
          assert(false);
      }
    }
  };

# undef swap_bytes

  template <class T, typename std::enable_if<std::is_arithmetic<T>::value, T>::type* = nullptr>
  inline auto val_as(const MDB_val& v) noexcept -> T {
    T ret;
    assert(v.mv_size == sizeof(T));
    memcpy(&ret, v.mv_data, sizeof(T));
    return ret;
  }

  template <class T, typename std::enable_if<std::is_pointer<T>::value,T>::type* = nullptr>
  inline auto val_as(const MDB_val& v) noexcept -> T {
    return static_cast<T>(v.mv_data);
  }

  enum class Dir { Asc, Desc };

  class DBIter final {
  protected:
    MDB_dbi dbi;
    MDB_txn* txn = nullptr;
    MDB_cursor* cur = nullptr;
    Dir dir;
    uint64_t n = 0;
    bool done = false, failed = false;
    std::optional<Cursor> to_key;

    inline auto reached_to_key() noexcept -> bool {
      if (!to_key) return false;
      auto val = to_key->val();
      const auto cmp = mdb_cmp(txn, dbi, &key, &val);
      return dir == Dir::Asc ? cmp >= 0 : cmp <= 0;
    }
  public:
    MDB_val key, value;
    struct End final { uint64_t n; };
    struct Iterator final {
      DBIter* db_iter;
      inline auto operator*() noexcept -> uint64_t {
        return **db_iter;
      }
      inline auto operator++() noexcept -> Iterator& {
        ++(*db_iter);
        return *this;
      }
      inline friend auto operator==(const Iterator& lhs, const End& rhs) noexcept -> bool {
        return lhs.db_iter->done || lhs.db_iter->n >= rhs.n;
      }
      inline friend auto operator!=(const Iterator& lhs, const End& rhs) noexcept -> bool {
        return !lhs.db_iter->done && lhs.db_iter->n < rhs.n;
      }
    };

    DBIter(
      MDB_dbi dbi,
      MDB_txn* txn,
      Dir dir,
      std::optional<MDB_val> from_key,
      std::optional<Cursor> to_key = {}
    );
    DBIter(
      MDB_dbi dbi,
      MDB_txn* txn,
      Dir dir,
      std::optional<Cursor> from_key = {},
      std::optional<Cursor> to_key = {}
    ) : DBIter(dbi, txn, dir, from_key.transform([](auto k){return k.val();}), to_key) {}
    DBIter(
      MDB_dbi dbi,
      MDB_txn* txn,
      Dir dir,
      std::pair<Cursor, uint64_t> from_kv,
      std::optional<Cursor> to_key = {}
    );

    DBIter(const DBIter&) = delete;
    auto operator=(const DBIter&) = delete;
    DBIter(DBIter &&from)
        : dbi(from.dbi), txn(from.txn), cur(from.cur), dir(from.dir), n(from.n),
          done(from.done), failed(from.failed), to_key(from.to_key),
          key(from.key), value(from.value) {
      from.cur = nullptr;
      from.done = true;
    };
    inline auto operator=(DBIter&& from) noexcept -> DBIter& {
      dbi = from.dbi;
      txn = from.txn;
      cur = from.cur;
      dir = from.dir;
      n = from.n;
      done = from.done;
      failed = from.failed;
      to_key = from.to_key;
      key = from.key;
      value = from.value;
      from.cur = nullptr;
      from.done = true;
      return *this;
    }
    virtual ~DBIter() {
      if (cur != nullptr) mdb_cursor_close(cur);
    }

    inline auto get_cursor() const noexcept -> std::optional<Cursor> {
      if (done) return {};
      return { Cursor(key) };
    }
    inline auto is_done() const noexcept -> bool {
      return done;
    }
    inline auto operator*() noexcept -> uint64_t {
      assert(!done);
      return val_as<uint64_t>(value);
    }
    auto operator++() noexcept -> DBIter&;
    inline auto begin() noexcept -> Iterator { return { this }; }
    inline auto end() noexcept -> End { return { ID_MAX }; }
    inline auto end_at(uint64_t n) noexcept -> End { return { n }; }

    inline friend auto operator==(const DBIter& lhs, const End& rhs) noexcept -> bool {
      return lhs.done || lhs.n >= rhs.n;
    }
    inline friend auto operator!=(const DBIter& lhs, const End& rhs) noexcept -> bool {
      return !lhs.done && lhs.n < rhs.n;
    }
  };
}
