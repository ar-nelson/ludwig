#include <uWebSockets/App.h>
#include <optparse.h>
#include <csignal>
#include "db.h++"
#include "controller.h++"
#include "webapp_routes.h++"

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
  parser.add_option("--db")
    .dest("db")
    .help("database filename")
    .set_default("ludwig.mdb");
  parser.add_option("-d", "--domain")
    .dest("domain")
    .help("site domain, with http:// or https:// prefix")
    .set_default("http://localhost");
  parser.add_help_option();

  const optparse::Values options = parser.parse_args(argc, argv);
  const auto port = std::atoi(options["port"].c_str());
  const auto dbfile = options["db"].c_str();
  if (port < 1 || port > 65535) {
    spdlog::critical("Invalid port: {}", options["port"]);
    return 1;
  }

  auto db = std::make_shared<Ludwig::DB>(dbfile);
  auto io = std::make_shared<asio::io_context>();
  auto controller = std::make_shared<Ludwig::Controller>(db, io);

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
