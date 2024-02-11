// (c) 2024, Interance GmbH & Co KG.

#include "database.hpp"
#include "database_actor.hpp"
#include "ec.hpp"
#include "types.hpp"

#include <caf/actor_system.hpp>
#include <caf/actor_system_config.hpp>
#include <caf/caf_main.hpp>
#include <caf/json_writer.hpp>
#include <caf/json_object.hpp>
#include <caf/net/http/with.hpp>
#include <caf/net/middleman.hpp>

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

using namespace std::literals;

namespace http = caf::net::http;

namespace {

std::string_view default_db_file = "items.db";

constexpr auto default_port = uint16_t{8080};

constexpr auto default_max_connections = size_t{128};

constexpr auto default_max_request_size = uint32_t{65'536};

constexpr std::string_view json_mime_type = "application/json";


std::atomic<bool> shutdown_flag;

void set_shutdown_flag(int) {
  shutdown_flag = true;
}

struct config : caf::actor_system_config {
  config(){
    opt_group{custom_options_, "global"}
      .add<std::string>("db-file,d", "path to the database file")
      .add<uint16_t>("port,p", "port to listen for incoming connections")
      .add<size_t>("max-connections,m", "limit for concurrent clients")
      .add<size_t>("max-request-size,r", "limit for single request size");
    opt_group{custom_options_, "tls"}
      .add<std::string>("key-file,k", "path to the private key file")
      .add<std::string>("cert-file,c", "path to the certificate file");
  }
};

bool is_ascii(caf::span<const std::byte> buffer) {
  auto pred = [](auto x) { return isascii(static_cast<unsigned char>(x)); };
  return std::all_of(buffer.begin(), buffer.end(), pred);
}

auto to_ascii(caf::span<const std::byte> buffer) {
  return std::string_view{reinterpret_cast<const char*>(buffer.data()),
                          buffer.size()};
}

class http_server : std::enable_shared_from_this<http_server> {
public:
  http_server(database_actor db_actor) : db_actor_(std::move(db_actor)) {
    writer_.skip_object_type_annotation(true);
  }

  ~http_server() {
    std::cerr << "http_server::~http_server\n";
  }

  void get(http::responder& res, int32_t key) {
    auto* self = res.self();
    auto prom = std::move(res).to_promise();
  //  auto thisptr = shared_from_this();
    self->request(db_actor_, 2s, caf::get_atom_v, key)
      .then(
        [this, prom](const item& value) mutable {
          respond_with_item(prom, value);
        },
        [this, prom](const caf::error& what) mutable {
          if (what == ec::no_such_item) {
            respond_with_error(prom, "no_such_item");
            return;
          }
          if (what == caf::sec::request_timeout) {
            respond_with_error(prom, "timeout");
            return;
          }
          respond_with_error(prom, "unexpected_database_result");
        });
  }

  void add(http::responder& res, int32_t key, const std::string& name,
           int32_t price) {
    auto* self = res.self();
    auto prom = std::move(res).to_promise();
    //  auto thisptr = shared_from_this();
    self->request(db_actor_, 2s, caf::get_atom_v, key)
      .then(
        [this, prom](const item& value) mutable {
          respond_with_item(prom, value);
        },
        [this, prom](const caf::error& what) mutable {
          respond_with_error(prom, what);
        });
  }

  void add(http::responder& res, int32_t key) {
    auto payload = res.payload();
    if (!is_ascii(payload)) {
      respond_with_error(res, "invalid_payload");
      return;
    }
    auto maybe_jval = caf::json_value::parse(to_ascii(payload));
    if (!maybe_jval || !maybe_jval->is_object()) {
      respond_with_error(res, "invalid_payload");
      return;
    }
    auto obj = maybe_jval->to_object();
    auto price = obj.value("price");
    auto name = obj.value("name");
    if (!name.is_string() || !price.is_integer()) {
      respond_with_error(res, "invalid_payload");
      return;
    }
    auto* self = res.self();
    auto prom = std::move(res).to_promise();
    self
      ->request(db_actor_, 2s, caf::put_atom_v, key,
                static_cast<int32_t>(price.to_integer()),
                std::string{name.to_string()})
      .then([prom]() mutable { prom.respond(http::status::created); },
            [this, prom](const caf::error& what) mutable {
              respond_with_error(prom, what);
            });
  }

  void inc(http::responder& res, int32_t key,int32_t amount) {
    auto* self = res.self();
    auto prom = std::move(res).to_promise();
    self->request(db_actor_, 2s, inc_atom_v, key, amount)
      .then([prom]() mutable { prom.respond(http::status::no_content); },
            [this, prom](const caf::error& what) mutable {
              respond_with_error(prom, what);
            });
  }

  void dec(http::responder& res, int32_t key, int32_t amount) {
    auto* self = res.self();
    auto prom = std::move(res).to_promise();
    self->request(db_actor_, 2s, dec_atom_v, key, amount)
      .then([prom]() mutable { prom.respond(http::status::no_content); },
            [this, prom](const caf::error& what) mutable {
              respond_with_error(prom, what);
            });
  }

  void del(http::responder& res, int32_t key) {
    auto* self = res.self();
    auto prom = std::move(res).to_promise();
    self->request(db_actor_, 2s, caf::delete_atom_v, key)
      .then([prom]() mutable { prom.respond(http::status::no_content); },
            [this, prom](const caf::error& what) mutable {
              respond_with_error(prom, what);
            });
  }

private:
  void respond_with_item(http::responder::promise& prom, const item& value) {
    writer_.reset();
    if (!writer_.apply(value)) {
      respond_with_error(prom, "serialization_failed"sv);
      return;
    }
    prom.respond(http::status::ok, json_mime_type, writer_.str());
  }

  template <class Responder>
  void respond_with_error(Responder& prom, std::string_view code) {
    std::string body = R"_({"code": ")_";
    body += code;
    body += "\"}";
    prom.respond(http::status::internal_server_error, json_mime_type, body);
  }

  template <class Responder>
  void respond_with_error(Responder& prom, const caf::error& reason) {
    if (reason.category() == caf::type_id_v<ec>) {
      auto code = to_string(static_cast<ec>(reason.code()));
      respond_with_error(prom, code);
      return;
    }
    if (reason == caf::sec::request_timeout) {
      respond_with_error(prom, "timeout"sv);
      return;
    }
    respond_with_error(prom, "internal_error"sv);
  }

  database_actor db_actor_;
  caf::json_writer writer_;
};

} // namespace

int caf_main(caf::actor_system& sys, const config& cfg) {
  // Do a regular shutdown for CTRL+C and SIGTERM.
  signal(SIGTERM, set_shutdown_flag);
  signal(SIGINT, set_shutdown_flag);
  // Database setup.
  auto db_file = caf::get_or(cfg, "db-file", default_db_file);
  auto db = std::make_shared<database>(db_file);
  if (auto err = db->open()) {
    std::cerr << "Failed to open the SQLite database: " << to_string(err)
              << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Database contains " << db->count() << " items" << std::endl;
  auto db_actor = spawn_database_actor(sys, db);
  // Read the configuration for the web server.
  auto port = caf::get_or(cfg, "port", default_port);
  auto pem = caf::net::ssl::format::pem;
  auto key_file = caf::get_as<std::string>(cfg, "tls.key-file");
  auto cert_file = caf::get_as<std::string>(cfg, "tls.cert-file");
  auto max_connections = caf::get_or(cfg, "max-connections",
                                     default_max_connections);
  auto max_request_size = caf::get_or(cfg, "max-request-size",
                                      default_max_request_size);
  if (!key_file != !cert_file) {
    std::cerr << "*** inconsistent TLS config: declare neither file or both\n";
    return EXIT_FAILURE;
  }
  // Open up a TCP port for incoming connections and start the server.
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
                 impl->get(res, key);
               })
        // Route for adding a new item to the database. The payload must be a
        // JSON object with the fields ""name" and "price".
        .route("/item/<arg>", http::method::post,
               [impl](http::responder& res, int32_t key) {
                 impl->add(res, key);
               })
        // Route for incrementing the available amount of an item.
        .route("/item/<arg>/inc/<arg>", http::method::put,
               [impl](http::responder& res, int32_t key, int32_t amount) {
                 impl->inc(res, key, amount);
               })
        // Route for decrementing the available amount of an item.
        .route("/item/<arg>/dec/<arg>", http::method::put,
               [impl](http::responder& res, int32_t key, int32_t amount) {
                 impl->dec(res, key, amount);
               })
        // Route for deleting an item from the database.
        .route("/item/<arg>", http::method::del,
               [impl](http::responder& res, int32_t key) {
                 impl->del(res, key);
               })
        // Start the server.
        .start();
  // Report any error to the user.
  if (!server) {
    std::cerr << "*** unable to run at port " << port << ": "
              << to_string(server.error()) << '\n';
    return EXIT_FAILURE;
  }
  // Wait for CTRL+C or SIGTERM and shut down the server.
  std::cerr << "*** press CTRL+C to terminate the server\n";
  while (!shutdown_flag)
    std::this_thread::sleep_for(250ms);
  std::cerr << "*** shutting down\n";
  server->dispose();
  anon_send_exit(db_actor, caf::exit_reason::user_shutdown);
  return EXIT_SUCCESS;
}

CAF_MAIN(caf::id_block::warehouse_backend, caf::net::middleman)
