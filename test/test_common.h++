#include "util/common.h++"
#include <lmdb.h>
#include <catch2/catch_test_macros.hpp>
#include <static_block.hpp>

using std::make_shared, std::nullopt, std::optional, std::pair, std::shared_ptr,
    std::string, std::string_view, std::vector;
using namespace std::literals::string_view_literals;
using namespace Ludwig;

static_block {
  spdlog::set_level(spdlog::level::debug);
}

struct TempFile {
  char* name;

  TempFile() {
    name = std::tmpnam(nullptr);
  }
  ~TempFile() {
    std::remove(name);
  }
};

struct TempDB {
  TempFile file;
  MDB_env* env;
  MDB_dbi dbi;

  TempDB() {
    int err = mdb_env_create(&env);
    if (err) throw std::runtime_error(mdb_strerror(err));
    err = mdb_env_set_maxdbs(env, 1);
    if (err) throw std::runtime_error(mdb_strerror(err));
    err = mdb_env_set_mapsize(env, 1024 * 1024 * 10);
    if (err) throw std::runtime_error(mdb_strerror(err));
    err = mdb_env_open(env, file.name, MDB_NOSUBDIR, 0600);
    if (err) throw std::runtime_error(mdb_strerror(err));
    MDB_txn* txn;
    err = mdb_txn_begin(env, nullptr, 0, &txn);
    if (err) throw std::runtime_error(mdb_strerror(err));
    err = mdb_dbi_open(txn, "test", MDB_CREATE | MDB_DUPSORT, &dbi);
    if (err) throw std::runtime_error(mdb_strerror(err));
    err = mdb_txn_commit(txn);
    if (err) throw std::runtime_error(mdb_strerror(err));
  }
  ~TempDB() {
    mdb_env_close(env);
    std::remove(fmt::format("{}-lock", file.name).c_str());
  }
};
