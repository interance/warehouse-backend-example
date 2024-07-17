// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include "database.hpp"
#include "item.hpp"
#include "types.hpp"

#include <memory>

// --(database-actor-begin)--
struct database_trait {
  using signatures = caf::type_list<
    // Retrieves an item from the database.
    caf::result<item>(get_atom, int32_t),
    // Adds a new item to the database.
    caf::result<void>(add_atom, int32_t, int32_t, std::string),
    // Increments the available count of an item.
    caf::result<int32_t>(inc_atom, int32_t, int32_t),
    // Decrements the available count of an item.
    caf::result<int32_t>(dec_atom, int32_t, int32_t),
    // Deletes an item from the database.
    caf::result<void>(del_atom, int32_t)>;
};

using database_actor = caf::typed_actor<database_trait>;
// --(database-actor-end)--

// --(spawn-database-actor-begin)--
std::pair<database_actor, item_events>
spawn_database_actor(caf::actor_system& sys, database_ptr db);
// --(spawn-database-actor-end)--
