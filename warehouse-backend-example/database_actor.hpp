// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include "database.hpp"
#include "types.hpp"

#include <caf/fwd.hpp>

#include <memory>

using item_event = std::shared_ptr<const item>;

using item_events = caf::async::publisher<item_event>;

using database_actor = caf::typed_actor<
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

std::pair<database_actor, item_events>
spawn_database_actor(caf::actor_system& sys, database_ptr db);
