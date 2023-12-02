#include "util/common.h++"
#include "util/rich_text.h++"
#include "services/db.h++"
#include "services/asio_http_client.h++"
#include "services/asio_event_bus.h++"
#include "services/lmdb_search_engine.h++"
#include "controllers/instance.h++"
#include "controllers/remote_media.h++"
#include "views/webapp.h++"
#include "views/media.h++"
#include <uWebSockets/App.h>
#include <asio.hpp>
#include <gzstream.h>
#include <optparse.h>
#include <fstream>
#include <csignal>

using namespace Ludwig;
using std::lock_guard, std::make_shared, std::mutex, std::optional, std::shared_ptr, std::vector;

static vector<us_listen_socket_t*> global_sockets;
static mutex global_sockets_mutex;

void signal_handler(int) {
  spdlog::warn("Caught signal, shutting down.");
  lock_guard<mutex> lock(global_sockets_mutex);
  for (auto* socket : global_sockets) us_listen_socket_close(0, socket);
}

int main(int argc, char** argv) {
  optparse::OptionParser parser =
    optparse::OptionParser().description("lemmy but better");
  parser.add_option("-p", "--port")
    .dest("port")
    .type("INT")
    .set_default(2023);
  parser.add_option("-d", "--domain")
    .dest("domain")
    .help("site domain, with http:// or https:// prefix")
    .set_default("http://localhost");
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
    .help("database dump file to import; if present, database file (--db) must not exist yet");
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
  const auto map_size = std::stoull(options["map_size"]);
  const auto rate_limit = (double)std::stoull(options["rate_limit"]);
  auto threads = std::stoull(options["threads"]);
  if (!threads) {
#   ifdef LUDWIG_DEBUG
    threads = 1;
#   else
    threads = std::thread::hardware_concurrency();
#   endif
  }
  const auto log_level = options["log_level"];
  spdlog::set_level(spdlog::level::from_str(log_level));

  optional<shared_ptr<SearchEngine>> search_engine;
  if (options["search"].starts_with("lmdb:")) {
    const auto filename = options["search"].substr(5);
    search_engine = make_shared<LmdbSearchEngine>(filename, map_size);
  } else if (options["search"] != "none") {
    spdlog::critical(R"(Invalid --search option: {} (must be "none" or "lmdb:filename.mdb")", options["search"]);
  }

  if (options.is_set_by_user("import")) {
    const auto importfile = options["import"];
    igzstream in(importfile.c_str(), std::ios_base::in);
    spdlog::info("Importing database dump from {}", importfile);
    DB db(dbfile, in, search_engine, map_size);
    spdlog::info("Import complete. You can now start Ludwig without --import.");
    return EXIT_SUCCESS;
  }

  const auto port = std::stoi(options["port"]);
  if (port < 1 || port > 65535) {
    spdlog::critical("Invalid port: {}", options["port"]);
    return 1;
  }

  AsioThreadPool pool(threads);
  auto rate_limiter = make_shared<KeyedRateLimiter>(rate_limit / 300.0, rate_limit);
  auto http_client = make_shared<AsioHttpClient>(pool.io);
  auto event_bus = make_shared<AsioEventBus>(pool.io);
  auto db = make_shared<DB>(dbfile, map_size);
  auto xml_ctx = make_shared<LibXmlContext>();
  auto rich_text = make_shared<RichTextParser>(xml_ctx);
  auto instance_c = make_shared<InstanceController>(db, http_client, rich_text, event_bus, search_engine);
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

  vector<std::thread> running_threads(threads - 1);
  auto run = [&] {
    uWS::App app;
    media_routes(app, remote_media_c);
    webapp_routes(app, instance_c, rich_text, rate_limiter);
    app.listen(port, [port](auto *listen_socket) {
      if (listen_socket) {
        lock_guard<mutex> lock(global_sockets_mutex);
        global_sockets.push_back(listen_socket);
        spdlog::info("Thread listening on port {}", port);
      }
    }).run();
  };
  for (size_t i = 1; i < threads; i++) running_threads.emplace_back(run);
  run();
  pool.stop();
  for (auto& th : running_threads) if (th.joinable()) th.join();

  if (!global_sockets.empty()) {
    spdlog::info("Shut down cleanly");
    return EXIT_SUCCESS;
  } else {
    spdlog::critical("Failed to listen on port {}", port);
    return EXIT_FAILURE;
  }
}
