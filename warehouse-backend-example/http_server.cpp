#include "http_server.hpp"

#include <caf/json_object.hpp>
#include <caf/json_value.hpp>
#include <caf/net/actor_shell.hpp>

using namespace std::literals;

using http_status = caf::net::http::status;

void http_server::get(responder& res, int32_t key) {
  auto* self = res.self();
  auto prom = std::move(res).to_promise();
  self->request(db_actor_, 2s, get_atom_v, key)
    .then([this,
           prom](const item& value) mutable { respond_with_item(prom, value); },
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

void http_server::add(responder& res, int32_t key, const std::string& name,
                      int32_t price) {
  auto* self = res.self();
  auto prom = std::move(res).to_promise();
  self->request(db_actor_, 2s, get_atom_v, key)
    .then([this,
           prom](const item& value) mutable { respond_with_item(prom, value); },
          [this, prom](const caf::error& what) mutable {
            respond_with_error(prom, what);
          });
}

void http_server::add(responder& res, int32_t key) {
  auto payload = res.payload();
  if (!caf::is_valid_utf8(payload)) {
    respond_with_error(res, "invalid_payload");
    return;
  }
  auto maybe_jval = caf::json_value::parse(caf::to_string_view(payload));
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
    ->request(db_actor_, 2s, add_atom_v, key,
              static_cast<int32_t>(price.to_integer()),
              std::string{name.to_string()})
    .then([prom]() mutable { prom.respond(http_status::created); },
          [this, prom](const caf::error& what) mutable {
            respond_with_error(prom, what);
          });
}

void http_server::inc(responder& res, int32_t key, int32_t amount) {
  auto* self = res.self();
  auto prom = std::move(res).to_promise();
  self->request(db_actor_, 2s, inc_atom_v, key, amount)
    .then([prom](int32_t) mutable { prom.respond(http_status::no_content); },
          [this, prom](const caf::error& what) mutable {
            respond_with_error(prom, what);
          });
}

void http_server::dec(responder& res, int32_t key, int32_t amount) {
  auto* self = res.self();
  auto prom = std::move(res).to_promise();
  self->request(db_actor_, 2s, dec_atom_v, key, amount)
    .then([prom](int32_t) mutable { prom.respond(http_status::no_content); },
          [this, prom](const caf::error& what) mutable {
            respond_with_error(prom, what);
          });
}

void http_server::del(responder& res, int32_t key) {
  auto* self = res.self();
  auto prom = std::move(res).to_promise();
  self->request(db_actor_, 2s, del_atom_v, key)
    .then([prom]() mutable { prom.respond(http_status::no_content); },
          [this, prom](const caf::error& what) mutable {
            respond_with_error(prom, what);
          });
}

void http_server::respond_with_item(responder::promise& prom,
                                    const item& value) {
  writer_.reset();
  if (!writer_.apply(value)) {
    respond_with_error(prom, "serialization_failed"sv);
    return;
  }
  prom.respond(http_status::ok, json_mime_type, writer_.str());
}
