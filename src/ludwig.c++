#include "util/common.h++"
#include "services/db.h++"
#include "services/asio_http_client.h++"
#include "controllers/instance.h++"
#include "views/webapp.h++"
#include <uWebSockets/App.h>
#include <asio.hpp>
#include <optparse.h>
#include <fstream>
#include <csignal>

static us_listen_socket_t* global_socket = nullptr;

void signal_handler(int) {
  spdlog::warn("Caught signal, shutting down.");
  if (global_socket != nullptr) {
    us_listen_socket_close(0, global_socket);
  }
}

int main(int argc, char** argv) {
  optparse::OptionParser parser =
    optparse::OptionParser().description("lemmy but better");
  parser.add_option("-p", "--port")
    .dest("port")
    .set_default("2023");
  parser.add_option("-d", "--domain")
    .dest("domain")
    .help("site domain, with http:// or https:// prefix")
    .set_default("http://localhost");
  parser.add_option("-s", "--map-size")
    .dest("map_size")
    .help("maximum database size, in MiB")
    .set_default("4096");
  parser.add_option("--db")
    .dest("db")
    .help("database filename, will be created if it does not exist")
    .set_default("ludwig.mdb");
  parser.add_option("--import")
    .dest("import")
    .help("database dump file to import; if present, database file (--db) must not exist yet");
  parser.add_option("--log-level")
    .dest("log_level")
    .help("log level (debug, info, warn, error, critical)")
    .set_default("info");
  parser.add_help_option();

  const optparse::Values options = parser.parse_args(argc, argv);
  const auto dbfile = options["db"].c_str();
  const auto map_size = std::stoull(options["map_size"]);
  const auto log_level = options["log_level"];
  spdlog::set_level(spdlog::level::from_str(log_level));
  if (options.is_set_by_user("import")) {
    const auto importfile = options["import"];
    std::ifstream in(importfile, std::ios_base::in);
    spdlog::info("Importing database dump from {}", importfile);
    Ludwig::DB db(dbfile, in, map_size);
    spdlog::info("Import complete. You can now start Ludwig without --import.");
    return EXIT_SUCCESS;
  }

  const auto port = std::stoi(options["port"]);
  if (port < 1 || port > 65535) {
    spdlog::critical("Invalid port: {}", options["port"]);
    return 1;
  }

  auto io = std::make_shared<asio::io_context>();
  auto ssl = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  ssl->set_default_verify_paths();
  auto http_client = std::make_shared<Ludwig::AsioHttpClient>(io, ssl);
  auto db = std::make_shared<Ludwig::DB>(dbfile, map_size);
  auto controller = std::make_shared<Ludwig::InstanceController>(db);

  struct sigaction sigint_handler { .sa_flags = 0 }, sigterm_handler { .sa_flags = 0 };
  sigint_handler.sa_handler = signal_handler;
  sigterm_handler.sa_handler = signal_handler;
  sigemptyset(&sigint_handler.sa_mask);
  sigemptyset(&sigterm_handler.sa_mask);
  sigaction(SIGINT, &sigint_handler, nullptr);
  sigaction(SIGTERM, &sigterm_handler, nullptr);

  uWS::App app;
  webapp_routes(app, controller);
  app.listen(port, [port](auto *listen_socket) {
    if (listen_socket) {
      global_socket = listen_socket;
      spdlog::info("Listening on port {}", port);
    }
  }).run();

  if (global_socket != nullptr) {
    spdlog::info("Shut down cleanly");
    return EXIT_SUCCESS;
  } else {
    spdlog::critical("Failed to listen on port {}", port);
    return EXIT_FAILURE;
  }
}
