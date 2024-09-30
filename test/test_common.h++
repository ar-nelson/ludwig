#include "util/common.h++"
#include "util/web.h++"
#include "services/http_client.h++"
#include <glib.h>
#include <map>
#include <filesystem>
#include <fstream>
#include <lmdb.h>
#include <catch2/catch_test_macros.hpp>
#include <static_block.hpp>

using std::make_shared, std::make_unique, std::nullopt, std::optional,
    std::pair, std::runtime_error, std::shared_ptr, std::string,
    std::string_view, std::vector;
using namespace std::chrono_literals;
using namespace std::literals::string_view_literals;
using namespace Ludwig;

#ifndef LUDWIG_TEST_ROOT
#error "String macro LUDWIG_TEST_ROOT must be defined when building tests"
#endif

static inline auto test_root() {
  return std::filesystem::absolute(LUDWIG_TEST_ROOT);
}

static_block {
  spdlog::set_level(spdlog::level::debug);
  g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_MASK, glib_log_handler, nullptr);
  g_log_set_handler("GLib-GObject", G_LOG_LEVEL_MASK, glib_log_handler, nullptr);
  g_log_set_handler("VIPS", G_LOG_LEVEL_MASK, glib_log_handler, nullptr);
}

static inline auto load_file(std::filesystem::path p) -> string {
  if (!std::filesystem::exists(p)) spdlog::error("Missing file: {}", p.string());
  REQUIRE(std::filesystem::exists(p));
  std::ostringstream ss;
  std::ifstream input(p, std::ios::binary);
  ss << input.rdbuf();
  return ss.str();
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
    if (err) throw runtime_error(mdb_strerror(err));
    err = mdb_env_set_maxdbs(env, 1);
    if (err) throw runtime_error(mdb_strerror(err));
    err = mdb_env_set_mapsize(env, 10 * MiB);
    if (err) throw runtime_error(mdb_strerror(err));
    err = mdb_env_open(env, file.name, MDB_NOSUBDIR | MDB_NOSYNC | MDB_NOMEMINIT, 0600);
    if (err) throw runtime_error(mdb_strerror(err));
    MDB_txn* txn;
    err = mdb_txn_begin(env, nullptr, 0, &txn);
    if (err) throw runtime_error(mdb_strerror(err));
    err = mdb_dbi_open(txn, "test", MDB_CREATE | MDB_DUPSORT, &dbi);
    if (err) throw runtime_error(mdb_strerror(err));
    err = mdb_txn_commit(txn);
    if (err) throw runtime_error(mdb_strerror(err));
  }
  ~TempDB() {
    mdb_env_close(env);
    std::remove(fmt::format("{}-lock", file.name).c_str());
  }
};

class MockHttpClient : public HttpClient {
private:
  std::unordered_map<string, std::tuple<uint16_t, string, string>> get_responses;
  class Response : public HttpClientResponse {
  private:
    uint16_t _status;
    string _mimetype, _body;
  public:
    Response(uint16_t status, string mimetype, string body) : _status(status), _mimetype(mimetype), _body(body) {}
    Response(uint16_t status) : _status(status), _mimetype("text/plain"), _body(http_status(status)) {}

    auto status() const -> uint16_t { return _status; };
    auto error() const -> optional<string_view> {
      return _status >= 400 ? optional(http_status(_status)) : nullopt;
    };
    auto header(string_view name) const -> string_view {
      if (name == "content-type") return _mimetype;
      else if (name == "location" && _status >= 300 && _status < 400) return _body;
      return { nullptr, 0 };
    };
    auto body() const -> string_view { return _body; };
  };
protected:
  auto fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) -> void {
    const auto rsp = get_responses.find(req.url.to_string());
    if (rsp == get_responses.end()) callback(make_unique<Response>(404));
    else if (req.method != "GET") callback(make_unique<Response>(405));
    else {
      const auto [status, mimetype, body] = rsp->second;
      callback(make_unique<Response>(status, mimetype, body));
    }
  }
public:
  auto on_get(string url, uint16_t status, string_view mimetype, string_view body) -> MockHttpClient* {
    get_responses.emplace(url, std::tuple(status, mimetype, body));
    return this;
  }
};
