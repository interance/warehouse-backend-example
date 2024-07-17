// (c) 2024, Interance GmbH & Co KG.

#include "database_actor.hpp"

#include "ec.hpp"
#include "item.hpp"
#include "types.hpp"

#include <caf/actor_from_state.hpp>
#include <caf/actor_system.hpp>
#include <caf/async/publisher.hpp>
#include <caf/error.hpp>
#include <caf/flow/observable_builder.hpp>
#include <caf/net/http/status.hpp>

namespace {

// --(database-actor-state-begin)--
struct database_actor_state {
  database_actor_state(database_actor::pointer self_ptr, database_ptr db_ptr,
                       item_events* events)
    : self(self_ptr), db(db_ptr), mcast(self) {
    *events = mcast.as_observable().to_publisher();
  }

  database_actor::behavior_type make_behavior();

  database_actor::pointer self;
  database_ptr db;
  caf::flow::multicaster<item_event> mcast;
};
// --(database-actor-state-end)--

database_actor::behavior_type database_actor_state::make_behavior() {
  return {
    [this](get_atom, int32_t id) -> caf::result<item> {
      if (auto value = db->get(id))
        return {std::move(*value)};
      return {caf::make_error(ec::no_such_item)};
    },
    // --(database-actor-state-add-begin)--
    [this](add_atom, int32_t id, int32_t price,
           const std::string& name) -> caf::result<void> {
      auto value = item{id, price, 0, std::move(name)};
      if (auto err = db->insert(value); err != ec::nil)
        return {caf::make_error(err)};
      mcast.push(std::make_shared<item>(std::move(value)));
      return {};
    },
    // --(database-actor-state-add-end)--
    [this](inc_atom, int32_t id, int32_t amount) -> caf::result<int32_t> {
      if (auto err = db->inc(id, amount); err != ec::nil)
        return {caf::make_error(err)};
      if (auto value = db->get(id)) {
        auto result = value->available;
        mcast.push(std::make_shared<item>(std::move(*value)));
        return result;
      }
      return {caf::make_error(ec::no_such_item)};
    },
    [this](dec_atom, int32_t id, int32_t amount) -> caf::result<int32_t> {
      if (auto err = db->dec(id, amount); err != ec::nil)
        return {caf::make_error(err)};
      if (auto value = db->get(id)) {
        auto result = value->available;
        mcast.push(std::make_shared<item>(std::move(*value)));
        return result;
      }
      return {caf::make_error(ec::no_such_item)};
    },
    [this](del_atom, int32_t id) -> caf::result<void> {
      auto value = db->get(id);
      if (!value)
        return {caf::make_error(ec::no_such_item)};
      if (auto err = db->del(id); err != ec::nil)
        return {caf::make_error(err)};
      value->available = 0;
      mcast.push(std::make_shared<item>(std::move(*value)));
      return caf::unit;
    },
  };
}

} // namespace

// --(spawn-database-actor-impl-begin)--
std::pair<database_actor, item_events>
spawn_database_actor(caf::actor_system& sys, database_ptr db) {
  // Note: the actor uses a blocking API (SQLite3) and thus should run in its
  //       own thread.
  using caf::actor_from_state;
  using caf::detached;
  item_events events;
  auto hdl = sys.spawn<detached>(actor_from_state<database_actor_state>, db,
                                 &events);
  return {hdl, std::move(events)};
}
// --(spawn-database-actor-impl-end)--
