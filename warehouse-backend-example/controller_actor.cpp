// (c) 2024, Interance GmbH & Co KG.

#include "controller_actor.hpp"

#include "log.hpp"

#include <caf/actor.hpp>
#include <caf/blocking_actor.hpp>
#include <caf/blocking_mail.hpp>
#include <caf/config.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/flow/byte.hpp>
#include <caf/flow/string.hpp>
#include <caf/json_reader.hpp>
#include <caf/net/socket.hpp>
#include <caf/net/tcp_accept_socket.hpp>
#include <caf/net/tcp_stream_socket.hpp>
#include <caf/scheduled_actor/flow.hpp>
#include <caf/typed_actor.hpp>

using namespace std::literals;

namespace {

struct command {
  // Either "inc" or "dec".
  std::string type;
  int32_t id = 0;
  int32_t amount = 0;

  bool valid() const noexcept {
    return type == "inc" || type == "dec";
  }
};

template <class Insepctor>
bool inspect(Insepctor& f, command& x) {
  return f.object(x).fields(f.field("type", x.type), f.field("id", x.id),
                            f.field("amount", x.amount));
}

} // namespace

caf::actor
spawn_controller_actor(caf::actor_system& sys, database_actor db_actor,
                       caf::net::acceptor_resource<std::byte> events) {
  return sys.spawn([events, db_actor](caf::event_based_actor* self) mutable {
    // Stop if the database actor terminates.
    self->monitor(db_actor, [self](const caf::error& reason) {
      log::info("controller lost the database actor: {}", reason);
      self->quit(reason);
    });
    // For each buffer pair, we create a new flow ...
    events.observe_on(self).for_each([self, db_actor](auto ev) {
      log::info("controller added a new client");
      auto [pull, push] = ev.data();
      pull
        .observe_on(self)
        // ... that converts the lines to commands ...
        .transform(caf::flow::byte::split_as_utf8_at('\n'))
        .map([self](const caf::cow_string& line) {
          log::debug("controller received line: {}", line.str());
          caf::json_reader reader;
          if (!reader.load(line.str())) {
            log::error("controller failed to parse JSON: {}",
                       reader.get_error());
            return std::shared_ptr<command>{}; // Invalid JSON.
          }
          auto ptr = std::make_shared<command>();
          if (!reader.apply(*ptr))
            return std::shared_ptr<command>{}; // Not a command.
          return ptr;
        })
        .concat_map([self, db_actor](std::shared_ptr<command> ptr) {
          // If the `map` step failed, inject an error message.
          if (ptr == nullptr || !ptr->valid()) {
            auto str = R"_({"error":"invalid command"})_"s;
            return self->make_observable()
              .just(caf::cow_string{std::move(str)})
              .as_observable();
          }
          // Send the command to the database actor and convert the
          // result message into an observable.
          caf::flow::observable<int32_t> result;
          if (ptr->type == "inc") {
            result = self->mail(inc_atom_v, ptr->id, ptr->amount)
                       .request(db_actor, 1s)
                       .as_observable();
          } else {
            result = self->mail(dec_atom_v, ptr->id, ptr->amount)
                       .request(db_actor, 1s)
                       .as_observable();
          }
          // On error, we return an error message to the client.
          return result
            .map([ptr](int32_t res) {
              log::debug("controller received result for {} -> {}", *ptr, res);
              auto str = R"_({"result":)_"s;
              str += std::to_string(res);
              str += '}';
              return caf::cow_string{std::move(str)};
            })
            .on_error_return([ptr](const caf::error& what) {
              log::debug("controller received an error for {} -> {}", *ptr,
                         what);
              auto str = R"_({"error":")_"s;
              str += to_string(what);
              str += R"_("})_";
              auto res = caf::cow_string{std::move(str)};
              return caf::expected<caf::cow_string>{std::move(res)};
            })
            .as_observable();
        })
        // ... disconnects if the client is too slow ...
        .on_backpressure_buffer(32)
        // ... and pushes the results back to the client as bytes.
        .transform(caf::flow::string::to_chars("\n"))
        .do_finally([] { log::info("controller lost connection to a client"); })
        .map([](char ch) { return static_cast<std::byte>(ch); })
        .subscribe(push);
    });
  });
}
