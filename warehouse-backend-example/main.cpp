// (c) 2024, Interance GmbH & Co KG.

#include "caf/event_based_actor.hpp"
#include "controller_actor.hpp"
#include "database.hpp"
#include "database_actor.hpp"
#include "http_server.hpp"
#include "log.hpp"
#include "types.hpp"

#include <caf/actor_system.hpp>
#include <caf/actor_system_config.hpp>
#include <caf/caf_main.hpp>
#include <caf/json_object.hpp>
#include <caf/json_writer.hpp>
#include <caf/net/acceptor_resource.hpp>
#include <caf/net/http/with.hpp>
#include <caf/net/middleman.hpp>
#include <caf/net/octet_stream/with.hpp>
#include <caf/net/tcp_accept_socket.hpp>
#include <caf/net/web_socket/frame.hpp>
#include <caf/net/web_socket/switch_protocol.hpp>
#include <caf/scheduled_actor/flow.hpp>

#include <sqlite3.h>

#include <csignal>
#include <cstdlib>
#include <string>
#include <string_view>

using namespace std::literals;

namespace http = caf::net::http;
namespace ws = caf::net::web_socket;

namespace {

std::string_view default_db_file = "items.db";

constexpr auto default_port = uint16_t{8080};

constexpr auto default_max_connections = size_t{128};

constexpr auto default_max_request_size = uint32_t{65'536};

constexpr auto default_max_pending_frames = size_t{32};

constexpr std::string_view json_mime_type = "application/json";

std::atomic<bool> shutdown_flag;

void set_shutdown_flag(int) {
  shutdown_flag = true;
}

struct config : caf::actor_system_config {
  config() {
    opt_group{custom_options_, "global"}
      .add<std::string>("db-file,d", "path to the database file")
      .add<uint16_t>("http-port,p", "port to listen for HTTP connections")
      .add<size_t>("max-connections,m", "limit for concurrent clients")
      .add<size_t>("max-request-size,r", "limit for single request size")
      .add<uint16_t>("cmd-port,P", "port to listen for (JSON) commands")
      .add<std::string>("cmd-addr,A", "bind address for the controller");
    opt_group{custom_options_, "tls"}
      .add<std::string>("key-file,k", "path to the private key file")
      .add<std::string>("cert-file,c", "path to the certificate file");
  }
};

// The actor for handling a single WebSocket connection.
void ws_worker(caf::event_based_actor* self,
               caf::net::accept_event<ws::frame> new_conn, item_events events) {
  using frame = ws::frame;
  auto [pull, push] = new_conn.data();
  auto writer = std::make_shared<caf::json_writer>();
  writer->skip_object_type_annotation(true);
  // We ignore whatever the client may send to us.
  pull.observe_on(self)
    .do_finally([] { log::info("WebSocket client disconnected"); })
    .subscribe(std::ignore);
  // Send all events as JSON objects to the client.
  events.observe_on(self)
    .filter([](const item_event& item) { return item != nullptr; })
    .map([writer](const item_event& item) mutable {
      writer->reset();
      if (!writer->apply(*item)) {
        log::error("failed to serialize an item event: {}",
                   writer->get_error());
        return frame{};
      }
      return frame{writer->str()};
    })
    .filter([](const frame& item) { return !item.empty(); })
    .on_backpressure_buffer(default_max_pending_frames)
    .subscribe(push);
}

// The actor for accepting incoming WebSocket connections.
void ws_server(caf::event_based_actor* self,
               caf::net::acceptor_resource<ws::frame> res, item_events events) {
  res.observe_on(self).for_each([self, events](auto new_conn) {
    log::info("WebSocket client connected");
    self->spawn(ws_worker, new_conn, events);
  });
}

} // namespace

int caf_main(caf::actor_system& sys, const config& cfg) {
  // Do a regular shutdown for CTRL+C and SIGTERM.
  signal(SIGTERM, set_shutdown_flag);
  signal(SIGINT, set_shutdown_flag);
  // Database setup.
  auto db_file = caf::get_or(cfg, "db-file", default_db_file);
  auto db = std::make_shared<database>(db_file);
  if (auto err = db->open()) {
    sys.println("Failed to open the SQLite database: {}", err);
    return EXIT_FAILURE;
  }
  sys.println("Database contains {} items", db->count());
  auto [db_actor, events] = spawn_database_actor(sys, db);
  // Spin up the controller if configured.
  auto ctrl = caf::actor{};
  if (auto cmd_port = caf::get_as<uint16_t>(cfg, "cmd-port")) {
    auto addr = caf::get_or(cfg, "cmd-addr", "0.0.0.0"sv);
    auto ctrl_server
      = caf::net::octet_stream::with(sys)
          // Bind to the user-defined port.
          .accept(*cmd_port)
          // Stop the server if our database actor terminates.
          .monitor(db_actor)
          // When started, run our worker actor to handle incoming connections.
          .start([&sys, db_actor = db_actor](
                   caf::net::acceptor_resource<std::byte> events) {
            spawn_controller_actor(sys, db_actor, std::move(events));
          });
    if (!ctrl_server) {
      sys.println("*** failed to start command server: {}",
                  ctrl_server.error());
      return EXIT_FAILURE;
    }
  }
  // Read the configuration for the web server.
  auto port = caf::get_or(cfg, "http-port", default_port);
  auto pem = caf::net::ssl::format::pem;
  auto key_file = caf::get_as<std::string>(cfg, "tls.key-file");
  auto cert_file = caf::get_as<std::string>(cfg, "tls.cert-file");
  auto max_connections = caf::get_or(cfg, "max-connections",
                                     default_max_connections);
  auto max_request_size = caf::get_or(cfg, "max-request-size",
                                      default_max_request_size);
  if (!key_file != !cert_file) {
    sys.println("*** inconsistent TLS config: declare neither file or both");
    return EXIT_FAILURE;
  }
  // Start the HTTP server.
  namespace ssl = caf::net::ssl;
  auto impl = std::make_shared<http_server>(db_actor);
  auto server
    = caf::net::http::with(sys)
        // Optionally enable TLS.
        .context(ssl::context::enable(key_file && cert_file)
                   .and_then(ssl::emplace_server(ssl::tls::v1_2))
                   .and_then(ssl::use_private_key_file(key_file, pem))
                   .and_then(ssl::use_certificate_file(cert_file, pem)))
        // Bind to the user-defined port.
        .accept(port)
        // Limit how many clients may be connected at any given time.
        .max_connections(max_connections)
        // Limit the maximum request size.
        .max_request_size(max_request_size)
        // Stop the server if our database actor terminates.
        .monitor(db_actor)
        // Route for retrieving an item from the database.
        .route("/item/<arg>", http::method::get,
               [impl](http::responder& res, int32_t key) {
                 log::debug("GET /item/{}", key);
                 impl->get(res, key);
               })
        // Route for adding a new item to the database. The payload must be a
        // JSON object with the fields ""name" and "price".
        .route("/item/<arg>", http::method::post,
               [impl](http::responder& res, int32_t key) {
                 log::debug("POST /item/{}, body: {}", key, res.body());
                 impl->add(res, key);
               })
        // Route for incrementing the available amount of an item.
        .route("/item/<arg>/inc/<arg>", http::method::put,
               [impl](http::responder& res, int32_t key, int32_t amount) {
                 log::debug("PUT /item/{}/inc/{}", key, amount);
                 impl->inc(res, key, amount);
               })
        // Route for decrementing the available amount of an item.
        .route("/item/<arg>/dec/<arg>", http::method::put,
               [impl](http::responder& res, int32_t key, int32_t amount) {
                 log::debug("PUT /item/{}/dec/{}", key, amount);
                 impl->dec(res, key, amount);
               })
        // Route for deleting an item from the database.
        .route("/item/<arg>", http::method::del,
               [impl](http::responder& res, int32_t key) {
                 log::debug("DELETE /item/{}", key);
                 impl->del(res, key);
               })
        // WebSocket route for subscribing to item events.
        .route("/events", http::method::get,
               ws::switch_protocol()
                 .on_request([](ws::acceptor<>& acc) { acc.accept(); })
                 .on_start([&sys, ev = events](auto res) {
                   sys.spawn(ws_server, res, ev);
                 }))
        // Start the server.
        .start();
  // Report any error to the user.
  if (!server) {
    sys.println("*** unable to run at port {}: {}", port, server.error());
    return EXIT_FAILURE;
  }
  // Wait for CTRL+C or SIGTERM and shut down the server.
  sys.println("*** running at port {}, press CTRL+C to terminate the server",
              port);
  while (!shutdown_flag)
    std::this_thread::sleep_for(250ms);
  sys.println("*** shutting down");
  server->dispose();
  anon_send_exit(db_actor, caf::exit_reason::user_shutdown);
  anon_send_exit(ctrl, caf::exit_reason::user_shutdown);
  return EXIT_SUCCESS;
}

CAF_MAIN(caf::id_block::warehouse_backend, caf::net::middleman)
