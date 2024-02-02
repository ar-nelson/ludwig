#include "util/rich_text.h++"
#include "util/zstd_db_dump.h++"
#include "services/db.h++"
#include "services/asio_http_client.h++"
#include "services/asio_event_bus.h++"
#include "services/lmdb_search_engine.h++"
#include "controllers/instance.h++"
#include "controllers/remote_media.h++"
#include "controllers/lemmy_api.h++"
#include "views/webapp.h++"
#include "views/media.h++"
#include "views/lemmy_api.h++"
#include <uWebSockets/App.h>
#include <asio.hpp>
#include <optparse.h>
#include <csignal>

using namespace Ludwig;
using std::lock_guard, std::make_shared, std::mutex, std::optional, std::pair,
    std::runtime_error, std::shared_ptr, std::stoull, std::thread,
    std::unique_ptr, std::vector;

namespace Ludwig {
  static vector<std::function<void()>> on_close;
  static mutex on_close_mutex;

  void signal_handler(int) {
    spdlog::warn("Caught signal, shutting down.");
    lock_guard<mutex> lock(on_close_mutex);
    for (auto& f : on_close) f();
  }

  // defined in util/setup.c++
  auto interactive_setup(bool admin_exists, bool default_board_exists) -> FirstRunSetup;
}

int main(int argc, char** argv) {
  optparse::OptionParser parser =
    optparse::OptionParser().description("lemmy but better");
  parser.add_option("--setup")
    .dest("setup")
    .nargs(0)
    .help("runs interactive first-run setup and exits; fails if server is already set up");
  parser.add_option("-p", "--port")
    .dest("port")
    .type("INT")
    .set_default(2023);
  parser.add_option("-s", "--map-size")
    .dest("map_size")
    .type("INT")
    .help("maximum database size, in MiB; also applies to search db if search type is lmdb (default = 4096)")
    .set_default(4096);
  parser.add_option("--db")
    .dest("db")
    .help("database filename, will be created if it does not exist (default = ludwig.mdb)")
    .set_default("ludwig.mdb");
  parser.add_option("--search")
    .dest("search")
    .help(R"(search provider, can be "none" or "lmdb:filename.mdb" (default = lmdb:search.mdb))")
    .set_default("lmdb:search.mdb");
  parser.add_option("--import")
    .dest("import")
    .help("database dump file to import; if present, database file (--db) must not exist yet; exits after importing");
  parser.add_option("--export")
    .dest("export")
    .help("database dump file to export to; exits after exporting");
  parser.add_option("--log-level")
    .dest("log_level")
    .help("log level (debug, info, warn, error, critical)")
    .set_default("info");
  parser.add_option("--rate-limit")
    .dest("rate_limit")
    .type("INT")
    .help("max requests per 5 minutes from a single IP (default = 3000)")
    .set_default(3000);
  parser.add_option("--threads")
    .dest("threads")
    .type("INT")
    .help("number of request handler threads (default = number of cores)")
    .set_default(0);
  parser.add_help_option();

  const optparse::Values options = parser.parse_args(argc, argv);
  const auto dbfile = options["db"].c_str();
  const auto map_size = stoull(options["map_size"]);
  const auto rate_limit = (double)stoull(options["rate_limit"]);
  auto threads = stoull(options["threads"]);
  if (!threads) {
#   ifdef LUDWIG_DEBUG
    threads = 1;
#   else
    threads = thread::hardware_concurrency();
#   endif
  }
  const auto log_level = options["log_level"];
  spdlog::set_level(spdlog::level::from_str(log_level));

  optional<shared_ptr<SearchEngine>> search_engine;
  if (options["search"].starts_with("lmdb:")) {
    const auto filename = options["search"].substr(5);
    search_engine = make_shared<LmdbSearchEngine>(filename, map_size);
  } else if (options["search"] != "none") {
    spdlog::critical(R"(Invalid --search option: {} (must be "none" or "lmdb:filename.mdb"))", options["search"]);
    return EXIT_FAILURE;
  }

  if (
    (int)options.is_set_by_user("setup") +
    (int)options.is_set_by_user("import") +
    (int)options.is_set_by_user("export")
    > 1
  ) {
    spdlog::critical("Only one of --setup, --import, or --export is allowed!");
    return EXIT_FAILURE;
  }

  if (options.is_set_by_user("import")) {
    const auto importfile = options["import"];
    uint64_t file_size = std::filesystem::file_size(importfile.c_str());
    unique_ptr<FILE, int(*)(FILE*)> f(fopen(importfile.c_str(), "rb"), &fclose);
    if (f == nullptr) {
      spdlog::critical("Could not open {}: {}", importfile, strerror(errno));
      return EXIT_FAILURE;
    }
    try {
      spdlog::info("Importing database dump from {}", importfile);
      zstd_db_dump_import(dbfile, f.get(), file_size, search_engine, map_size);
      spdlog::info("Import complete. You can now start Ludwig without --import.");
      return EXIT_SUCCESS;
    } catch (const runtime_error& e) {
      spdlog::critical("Import failed: {}", e.what());
      return EXIT_FAILURE;
    }
  }

  auto db = make_shared<DB>(dbfile, map_size);
  if (options.is_set_by_user("export")) {
    const auto exportfile = options["export"];
    unique_ptr<FILE, int(*)(FILE*)> f(fopen(exportfile.c_str(), "wb"), &fclose);
    if (f == nullptr) {
      spdlog::critical("Could not open {}: {}", exportfile, strerror(errno));
      return EXIT_FAILURE;
    }
    try {
      spdlog::info("Exporting database dump to {}", exportfile);
      auto txn = db->open_read_txn();
      zstd_db_dump_export(txn, [&](auto&& buf, auto sz) {
        fwrite(buf.get(), 1, sz, f.get());
      });
      spdlog::info("Export complete.");
      return EXIT_SUCCESS;
    } catch (const runtime_error& e) {
      spdlog::critical("Export failed: {}", e.what());
      return EXIT_FAILURE;
    }
  }

  bool first_run = false, admin_exists = true, default_board_exists = true;
  {
    auto txn = db->open_read_txn();
    if (!txn.get_setting_int(SettingsKey::setup_done)) first_run = true;
    if (txn.get_admin_list().empty()) admin_exists = false;
  }

  if (options.is_set_by_user("setup")) {
    if (!first_run) {
      spdlog::critical("This server is already configured; cannot run interactive setup.");
      return EXIT_FAILURE;
    }
    auto setup = interactive_setup(admin_exists, default_board_exists);
    auto instance = make_shared<InstanceController>(db, nullptr, make_shared<DummyEventBus>(), search_engine);
    instance->first_run_setup(std::move(setup));
    puts("\nFirst-run setup complete. You can now start Ludwig without --setup.");
    return EXIT_SUCCESS;
  }

  const auto port = std::stoi(options["port"]);
  if (port < 1 || port > 65535) {
    spdlog::critical("Invalid port: {}", options["port"]);
    return EXIT_FAILURE;
  }

  optional<pair<Hash, Salt>> first_run_admin_password = {};
  if (first_run) {
    if (admin_exists) {
      spdlog::warn("The server is not yet configured, but an admin user exists.");
      spdlog::warn("Log in as an admin user to complete first-run setup, or CTRL-C and re-run with --setup.");
    } else {
      SecretString admin_password = generate_password();
      spdlog::critical("The server is not yet configured, and no users exist yet.");
      spdlog::critical("A temporary admin user has been generated.");
      spdlog::critical("USERNAME: {}", FIRST_RUN_ADMIN_USERNAME);
      spdlog::critical("PASSWORD: {}", admin_password.data);
      Hash hash;
      Salt salt;
      RAND_bytes((uint8_t*)salt.bytes()->Data(), 16);
      InstanceController::hash_password(std::move(admin_password), salt.bytes()->Data(), (uint8_t*)hash.bytes()->Data());
      first_run_admin_password = pair(hash, salt);
      spdlog::critical(
        "Go to http://localhost:{} and log in as this user to complete first-run setup, or CTRL-C and re-run with --setup.",
        port
      );
    }
  }

  AsioThreadPool pool(threads);
  auto rate_limiter = make_shared<KeyedRateLimiter>(rate_limit / 300.0, rate_limit);
  auto http_client = make_shared<AsioHttpClient>(pool.io);
  auto event_bus = make_shared<AsioEventBus>(pool.io);
  auto xml_ctx = make_shared<LibXmlContext>();
  auto instance_c = make_shared<InstanceController>(db, http_client, event_bus, search_engine, first_run_admin_password);
  auto api_c = make_shared<Lemmy::ApiController>(instance_c);
  auto remote_media_c = make_shared<RemoteMediaController>(
    db, http_client, xml_ctx, event_bus,
    [&pool](auto f) { pool.post(std::move(f)); },
    search_engine
  );

  struct sigaction sigint_handler { .sa_flags = 0 }, sigterm_handler { .sa_flags = 0 };
  sigint_handler.sa_handler = signal_handler;
  sigterm_handler.sa_handler = signal_handler;
  sigemptyset(&sigint_handler.sa_mask);
  sigemptyset(&sigterm_handler.sa_mask);
  sigaction(SIGINT, &sigint_handler, nullptr);
  sigaction(SIGTERM, &sigterm_handler, nullptr);

  vector<thread> running_threads(threads - 1);
  auto run = [&] {
    uWS::App app;
    media_routes(app, remote_media_c);
    webapp_routes(app, instance_c, rate_limiter);
    Lemmy::api_routes(app, api_c, rate_limiter);
    app.listen(port, [port, app = &app](auto *listen_socket) {
      if (listen_socket) {
        lock_guard<mutex> lock(on_close_mutex);
        on_close.push_back([app] { app->close(); });
        spdlog::info("Thread listening on port {}", port);
      }
    }).run();
  };
  for (size_t i = 1; i < threads; i++) running_threads.emplace_back(run);
  run();
  pool.stop();
  for (auto& th : running_threads) if (th.joinable()) th.join();

  if (!on_close.empty()) {
    spdlog::info("Shut down cleanly");
    return EXIT_SUCCESS;
  } else {
    spdlog::critical("Failed to listen on port {}", port);
    return EXIT_FAILURE;
  }
}
