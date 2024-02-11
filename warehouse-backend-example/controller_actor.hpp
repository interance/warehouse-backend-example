// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include "caf/net/fwd.hpp"
#include "database_actor.hpp"
#include "types.hpp"

#include <caf/fwd.hpp>
#include <caf/net/fwd.hpp>

caf::actor spawn_controller_actor(caf::actor_system& sys,
                                  database_actor db_actor,
                                  caf::net::tcp_accept_socket fd);
