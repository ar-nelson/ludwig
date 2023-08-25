#include "lmdb-safe.hh"
#include <memory>
#include <string>

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
  std::shared_ptr<MDBEnv> env;
  MDBDbi dbi;

  TempDB() {
    env = getMDBEnv(file.name, MDB_NOSUBDIR, 0600);
    dbi = env->openDB("test", MDB_CREATE);
  }
};
