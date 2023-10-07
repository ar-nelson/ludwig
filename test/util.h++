#include "util/common.h++"
#include <lmdb.h>
#include <memory>

using namespace std::literals::string_view_literals;

struct TempFile {
  char name[L_tmpnam];

  TempFile() {
    std::tmpnam(name);
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
    err = mdb_dbi_open(txn, "test", MDB_CREATE, &dbi);
    if (err) throw std::runtime_error(mdb_strerror(err));
    err = mdb_txn_commit(txn);
    if (err) throw std::runtime_error(mdb_strerror(err));
  }
  ~TempDB() {
    mdb_env_close(env);
  }
};
