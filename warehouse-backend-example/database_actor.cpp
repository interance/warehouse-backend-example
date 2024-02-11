// (c) 2024, Interance GmbH & Co KG.

#include "database_actor.hpp"

#include "ec.hpp"
#include "item.hpp"
#include "types.hpp"

#include <caf/actor_from_state.hpp>
#include <caf/actor_system.hpp>
#include <caf/error.hpp>
#include <caf/net/http/status.hpp>

namespace {

struct database_actor_state {
  database_actor_state(database_actor::pointer self_ptr, database_ptr db_ptr)
    : self(self_ptr), db(db_ptr) {
    // nop
  }

  database_actor::behavior_type make_behavior() {
    return {
      [this](caf::get_atom, int32_t id) -> caf::result<item> {
        if (auto value = db->get(id))
          return {std::move(*value)};
        return {caf::make_error(ec::no_such_item)};
      },
      [this](caf::put_atom, int32_t id, int32_t price,
             const std::string& name) -> caf::result<void> {
        if (auto err = db->insert(item{id, price, 0, name}); err != ec::nil)
          return {caf::make_error(err)};
        return {};
      },
      [this](inc_atom, int32_t id, int32_t amount) -> caf::result<void> {
        if (auto err = db->inc(id, amount); err != ec::nil)
          return {caf::make_error(err)};
        return caf::unit;
      },
      [this](dec_atom, int32_t id, int32_t amount) -> caf::result<void> {
        if (auto err = db->dec(id, amount); err != ec::nil)
          return {caf::make_error(err)};
        return caf::unit;
      },
      [this](caf::delete_atom, int32_t id) -> caf::result<void> {
        if (auto err = db->del(id); err != ec::nil)
          return {caf::make_error(err)};
        return caf::unit;
      },
    };
  }

  database_actor::pointer self;
  database_ptr db;
};

} // namespace

database_actor spawn_database_actor(caf::actor_system& sys, database_ptr db) {
  // Note: the actor uses a blocking API (SQLite3) and thus should run in its
  //       own thread.
  using caf::detached;
  return sys.spawn<detached>(caf::actor_from_state<database_actor_state>, db);
}

