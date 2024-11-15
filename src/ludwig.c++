#include "glib.h"
#include "spdlog/common.h"
#include "util/common.h++"
#include "util/rich_text.h++"
#include "db/db.h++"
#include "services/asio_http_client.h++"
#include "services/asio_event_bus.h++"
#include "services/lmdb_search_engine.h++"
#include "controllers/board_controller.h++"
#include "controllers/dump_controller.h++"
#include "controllers/first_run_controller.h++"
#include "controllers/lemmy_api_controller.h++"
#include "controllers/post_controller.h++"
#include "controllers/remote_media_controller.h++"
#include "controllers/search_controller.h++"
#include "controllers/session_controller.h++"
#include "controllers/site_controller.h++"
#include "controllers/user_controller.h++"
#include "views/webapp/routes.h++"
#include "views/media_routes.h++"
#include "views/lemmy_api_routes.h++"
#include "vips/vips.h"
#include <uWebSockets/App.h>
#include <asio.hpp>
#include <optparse.h>
#include <csignal>

using namespace Ludwig;
using std::lock_guard, std::make_shared, std::mutex, std::optional, std::pair,
    std::runtime_error, std::shared_ptr, std::stoull, std::string, std::thread,
    std::unique_ptr, std::vector;

namespace Ludwig {
  static vector<std::function<void()>> on_close;
  static mutex on_close_mutex;

  void signal_handler(int) {
    spdlog::warn("Caught signal, shutting down.");
    lock_guard<mutex> lock(on_close_mutex);
    for (auto& f : on_close) f();
  }
}

int main(int argc, char** argv) {
  auto parser = optparse::OptionParser()
    .version(string(VERSION))
    .description("Web forum server, compatible with Lemmy");
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
    .type("FILE.mdb")
    .help("database filename, will be created if it does not exist (default = ludwig.mdb)")
    .set_default("ludwig.mdb");
  parser.add_option("--search")
    .dest("search")
    .help(R"(search provider, can be "none" or "lmdb:filename.mdb" (default = lmdb:search.mdb))")
    .set_default("lmdb:search.mdb");
  parser.add_option("--import")
    .dest("import")
    .type("FILE.zst")
    .help("database dump file to import; if present, database file (--db) must not exist yet; exits after importing");
  parser.add_option("--export")
    .dest("export")
    .type("FILE.zst")
    .help("database dump file to export to; exits after exporting");
  parser.add_option("--log-level")
    .dest("log_level")
    .help("log level (debug, info, warn, error, critical)")
    .set_default("info");
  parser.add_option("-r", "--rate-limit")
    .dest("rate_limit")
    .type("INT")
    .help("max requests per 5 minutes from a single IP (default = 3000)")
    .set_default(3000);
  parser.add_option("-t", "--threads")
    .dest("threads")
    .type("INT")
    .help("number of request handler threads (default = number of cores)")
    .set_default(0);
  parser.add_option("--unsafe-https")
    .nargs(0)
    .dest("unsafe_https")
    .help("don't validate HTTPS certificates when making requests to other servers");
  parser.add_option("--unsafe-local-requests")
    .nargs(0)
    .dest("unsafe_local_requests")
    .help("don't block HTTP requests to local network IP addresses");
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

  shared_ptr<SearchEngine> search_engine = nullptr;
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
      DumpController::import_dump(dbfile, f.get(), file_size, search_engine, map_size);
      spdlog::info("Import complete. You can now start Ludwig without --import.");
      return EXIT_SUCCESS;
    } catch (const runtime_error& e) {
      spdlog::critical("Import failed: {}", e.what());
      return EXIT_FAILURE;
    }
  }

  auto db = make_shared<DB>(dbfile, map_size);
  auto dump_controller = make_shared<DumpController>();
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
      for (auto chunk : dump_controller->export_dump(txn)) {
        fwrite(chunk.data(), 1, chunk.size(), f.get());
      };
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
    if (!txn.get_setting_int(SettingsKey::default_board_id)) default_board_exists = false;
  }

  if (options.is_set_by_user("setup")) {
    if (!first_run) {
      spdlog::critical("This server is already configured; cannot run interactive setup.");
      return EXIT_FAILURE;
    }
    auto site = make_shared<SiteController>(db);
    auto boards = make_shared<BoardController>(site);
    auto users = make_shared<UserController>(site);
    FirstRunController(users, boards, site).first_run_setup(
      db->open_write_txn_sync(),
      FirstRunController::interactive_setup(admin_exists, default_board_exists)
    );
    puts("\nFirst-run setup complete. You can now start Ludwig without --setup.");
    return EXIT_SUCCESS;
  }

  const auto port = std::stoi(options["port"]);
  if (port < 1 || port > 65535) {
    spdlog::critical("Invalid port: {}", options["port"]);
    return EXIT_FAILURE;
  }

  optional<SecretString> first_run_admin_password = {};
  if (first_run) {
    if (admin_exists) {
      spdlog::warn("The server is not yet configured, but an admin user exists.");
      spdlog::warn("Log in as an admin user to complete first-run setup, or CTRL-C and re-run with --setup.");
    } else {
      first_run_admin_password = generate_password();
      spdlog::critical("The server is not yet configured, and no users exist yet.");
      spdlog::critical("A temporary admin user has been generated.");
      spdlog::critical("USERNAME: {}", FIRST_RUN_ADMIN_USERNAME);
      spdlog::critical("PASSWORD: {}", first_run_admin_password->data);
      spdlog::critical(
        "Go to http://localhost:{} and log in as this user to complete first-run setup, or CTRL-C and re-run with --setup.",
        port
      );
    }
  }

  if (VIPS_INIT(argv[0])) {
    vips_error_exit(nullptr);
  }
  g_log_set_handler("VIPS", G_LOG_LEVEL_MASK, glib_log_handler, nullptr);

  AsioThreadPool pool(threads);
  auto rate_limiter = make_shared<KeyedRateLimiter>(rate_limit / 300.0, rate_limit);
  auto http_client = make_shared<AsioHttpClient>(pool.io, 1000,
    options.is_set_by_user("unsafe_https") ? UnsafeHttps::UNSAFE : UnsafeHttps::SAFE,
    options.is_set_by_user("unsafe_local_requests") ? UnsafeLocalRequests::UNSAFE : UnsafeLocalRequests::SAFE
  );
  auto event_bus = make_shared<AsioEventBus>(pool.io);
  auto xml_ctx = make_shared<LibXmlContext>();
  auto site_c = make_shared<SiteController>(db, event_bus);
  auto board_c = make_shared<BoardController>(site_c, event_bus);
  auto user_c = make_shared<UserController>(site_c, event_bus);
  auto post_c = make_shared<PostController>(site_c, event_bus);
  auto search_c = make_shared<SearchController>(db, search_engine, event_bus);
  auto session_c = make_shared<SessionController>(db, site_c, user_c, std::move(first_run_admin_password));
  auto first_run_c = make_shared<FirstRunController>(user_c, board_c, site_c);
  auto dump_c = make_shared<DumpController>();
  auto api_c = make_shared<Lemmy::ApiController>(site_c, user_c, session_c, board_c, post_c, search_c, first_run_c);
  auto remote_media_c = make_shared<RemoteMediaController>(
    pool.io, db, http_client, xml_ctx, event_bus,
    [&pool](auto f) { pool.post(std::move(f)); }
  );

  struct sigaction sigint_handler { .sa_flags = 0 }, sigterm_handler { .sa_flags = 0 };
  sigint_handler.sa_handler = signal_handler;
  sigterm_handler.sa_handler = signal_handler;
  sigemptyset(&sigint_handler.sa_mask);
  sigemptyset(&sigterm_handler.sa_mask);
  sigaction(SIGINT, &sigint_handler, nullptr);
  sigaction(SIGPIPE, &sigterm_handler, nullptr);
  sigaction(SIGTERM, &sigterm_handler, nullptr);

  vector<thread> running_threads(threads - 1);
  auto run = [&] {
    uWS::App app;
    define_media_routes(app, remote_media_c);
    define_webapp_routes(
      app,
      db,
      site_c,
      session_c,
      post_c,
      board_c,
      user_c,
      search_c,
      first_run_c,
      dump_c,
      rate_limiter
    );
    Lemmy::define_api_routes(app, db, api_c, rate_limiter);
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
  vips_shutdown();

  if (!on_close.empty()) {
    spdlog::info("Shut down cleanly");
    return EXIT_SUCCESS;
  } else {
    spdlog::critical("Failed to listen on port {}", port);
    return EXIT_FAILURE;
  }
}
