// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include "database_actor.hpp"

#include <caf/error.hpp>
#include <caf/json_writer.hpp>
#include <caf/net/http/responder.hpp>
#include <caf/typed_actor.hpp>

#include <cstdint>
#include <string_view>

// --(http-server-utility-begin)--
/// Bridges between HTTP requests and the database actor.
class http_server {
public:
  using responder = caf::net::http::responder;

  http_server(database_actor db_actor) : db_actor_(std::move(db_actor)) {
    writer_.skip_object_type_annotation(true);
  }

  static constexpr std::string_view json_mime_type = "application/json";

  void get(responder& res, int32_t key);

  void add(responder& res, int32_t key, const std::string& name, int32_t price);

  void add(responder& res, int32_t key);

  void inc(responder& res, int32_t key, int32_t amount);

  void dec(responder& res, int32_t key, int32_t amount);

  void del(responder& res, int32_t key);
// --(http-server-utility-end)--

private:
  void respond_with_item(responder::promise& prom, const item& value);

  template <class Responder>
  void respond_with_error(Responder& prom, std::string_view code) {
    using status = caf::net::http::status;
    std::string body = R"_({"code": ")_";
    body += code;
    body += "\"}";
    prom.respond(status::internal_server_error, json_mime_type, body);
  }

  template <class Responder>
  void respond_with_error(Responder& prom, const caf::error& reason) {
    using namespace std::literals;
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
