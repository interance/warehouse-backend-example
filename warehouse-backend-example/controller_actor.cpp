// (c) 2024, Interance GmbH & Co KG.

#include "controller_actor.hpp"

#include <caf/actor.hpp>
#include <caf/blocking_actor.hpp>
#include <caf/blocking_mail.hpp>
#include <caf/config.hpp>
#include <caf/json_reader.hpp>
#include <caf/net/socket.hpp>
#include <caf/net/tcp_accept_socket.hpp>
#include <caf/net/tcp_stream_socket.hpp>
#include <caf/typed_actor.hpp>

// Platform-dependent code for `select`
#ifdef CAF_WINDOWS
#  include <winsock2.h>
#else
#  include <sys/select.h>
#endif

using namespace std::literals;

namespace {

constexpr size_t max_workers = 3;

constexpr size_t max_buffer_size = 1024; // 1KB

// Function to wait until the socket becomes ready for reading
bool wait_ready(caf::net::socket_id fd, caf::timespan timeout) {
  // Initialize the socket set.
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  // Set up the timeout
  using std::chrono::duration_cast;
  timeval tv;
  tv.tv_sec = duration_cast<std::chrono::seconds>(timeout).count();
  tv.tv_usec = duration_cast<std::chrono::microseconds>(timeout).count()
               % 1'000'000;
  // Block until the socket is ready for reading or the timeout occurs.
  int ready = select(fd + 1, &readfds, nullptr, nullptr, &tv);
  if (ready < 0)
    throw std::logic_error("Error in select()");
  return ready > 0;
}

struct command {
  // Either "inc" or "dec".
  std::string type;
  int32_t id = 0;
  int32_t amount = 0;
};

template <class Insepctor>
bool inspect(Insepctor& f, command& x) {
  return f.object(x).fields(f.field("type", x.type), f.field("id", x.id),
                            f.field("amount", x.amount));
}

void worker_impl(caf::blocking_actor* self, database_actor db_actor,
                 caf::net::tcp_stream_socket fd) {
  if (auto err = caf::net::nonblocking(fd, true)) {
    self->fail_state(std::move(err));
    return;
  }
  caf::json_reader reader;
  auto received = size_t{0};
  caf::byte_buffer buffer;
  buffer.resize(max_buffer_size);
  auto done = false;
  self->monitor(db_actor);
  do {
    // Wait for socket activity, but wake up every 100ms to check the mailbox.
    auto socket_ready = wait_ready(fd.id, 100ms);
    // Drain the mailbox. Passing 0s as the timeout basically makes `receive`
    // non-blocking and calls the timeout handler if no message is available.
    auto receiving = true;
    self->receive_while(receiving)(
      [&](caf::exit_msg& msg) {
        self->fail_state(std::move(msg.reason));
        done = true;
        receiving = false;
      },
      [&](caf::down_msg& msg) {
        if (msg.source == db_actor) {
          self->fail_state(std::move(msg.reason));
          done = true;
          receiving = false;
        }
      },
      caf::after(0s) >>
        [&] {
          receiving = false;
          if (!socket_ready)
            return;
          // Try to fill up the buffer.
          auto res = caf::net::read(fd,
                                    caf::make_span(buffer).subspan(received));
          if (res < 0) {
            if (caf::net::last_socket_error_is_temporary()) {
              // Simply try again later.
              return;
            }
            self->fail_state(caf::sec::socket_operation_failed);
            done = true;
            return;
          }
          if (res == 0) {
            self->fail_state(caf::sec::socket_disconnected);
            done = true;
            return;
          }
          received += static_cast<size_t>(res);
          // Look for the last newline character in the buffer.
          auto bytes = caf::make_span(buffer).subspan(0, received);
          auto pos = std::find(bytes.rbegin(), bytes.rend(), std::byte{'\n'});
          if (pos == bytes.rend()) {
            // Abort if the buffer is full without finding a newline.
            if (received == max_buffer_size) {
              self->fail_state(caf::sec::runtime_error);
              done = true;
            }
            return;
          }
          // Check whether the input is valid UTF-8.
          bytes = bytes.subspan(0, bytes.size() - (pos - bytes.rbegin()));
          if (!caf::is_valid_utf8(bytes)) {
            self->fail_state(caf::sec::runtime_error);
            done = true;
            return;
          }
          // Split the input into lines and process each line.
          std::vector<std::string_view> lines;
          auto str = caf::to_string_view(bytes);
          caf::split(lines, str, caf::is_any_of("\n"));
          for (auto line : lines) {
            // Parse the line as JSON.
            if (!reader.load(line)) {
              self->fail_state(caf::sec::runtime_error);
              done = true;
              return;
            }
            // Parse the JSON object as a command.
            command cmd;
            if (!reader.apply(cmd) || cmd.type != "inc" && cmd.type != "dec") {
              self->fail_state(caf::sec::runtime_error);
              done = true;
              return;
            }
            // Send the command to the database actor.
            if (cmd.type == "inc") {
              self->mail(inc_atom_v, cmd.id, cmd.amount).send(db_actor);
            } else {
              self->mail(dec_atom_v, cmd.id, cmd.amount).send(db_actor);
            }
          }
        });
  } while (!done);
}

void controller_impl(caf::blocking_actor* self, database_actor db_actor,
                     caf::net::tcp_accept_socket fd) {
  if (auto err = caf::net::nonblocking(fd, true)) {
    self->fail_state(std::move(err));
    return;
  }
  auto done = false;
  self->monitor(db_actor);
  std::set<caf::actor_addr> workers;
  do {
    // If we have reached the maximum worker count, we simply wait until a new
    // message arrives. Otherwise, we wait up to 100ms on the socket before
    // checking the mailbox.
    auto socket_ready = false;
    if (workers.size() >= max_workers)
      self->await_data();
    else
      socket_ready = wait_ready(fd.id, 100ms);
    // Drain the mailbox. Passing 0s as the timeout basically makes `receive`
    // non-blocking and calls the timeout handler if no message is available.
    auto receiving = true;
    self->receive_while(receiving)(
      [&](caf::exit_msg& msg) {
        self->fail_state(std::move(msg.reason));
        done = true;
        receiving = false;
      },
      [&](caf::down_msg& msg) {
        if (msg.source == db_actor) {
          self->fail_state(std::move(msg.reason));
          done = true;
          receiving = false;
          return;
        }
        workers.erase(msg.source);
      },
      caf::after(0s) >>
        [&] {
          receiving = false;
          if (!socket_ready)
            return;
          if (auto conn_fd = caf::net::accept(fd)) {
            auto worker = self->spawn(worker_impl, db_actor, *conn_fd);
            workers.insert(worker.address());
          }
        });
  } while (!done);
}

} // namespace

caf::actor spawn_controller_actor(caf::actor_system& sys,
                                  database_actor db_actor,
                                  caf::net::tcp_accept_socket fd) {
  return sys.spawn(controller_impl, db_actor, fd);
}
