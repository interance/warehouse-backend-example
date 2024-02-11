// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include "database.hpp"
#include "types.hpp"

#include <caf/fwd.hpp>

using database_actor = caf::typed_actor<
  // Retrieves an item from the database.
  caf::result<item>(get_atom, int32_t),
  // Adds a new item to the database.
  caf::result<void>(add_atom, int32_t, int32_t, std::string),
  // Increments the available count of an item.
  caf::result<void>(inc_atom, int32_t, int32_t),
  // Decrements the available count of an item.
  caf::result<void>(dec_atom, int32_t, int32_t),
  // Deletes an item from the database.
  caf::result<void>(del_atom, int32_t)>;

database_actor spawn_database_actor(caf::actor_system& sys, database_ptr db);
