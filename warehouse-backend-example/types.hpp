// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include <caf/type_id.hpp>

#include <cstdint>

class item;
enum class ec : uint8_t;

CAF_BEGIN_TYPE_ID_BLOCK(warehouse_backend, first_custom_type_id)

  CAF_ADD_TYPE_ID(warehouse_backend, (ec))
  CAF_ADD_TYPE_ID(warehouse_backend, (item))

  // Used to decrement the available count of an item.
  CAF_ADD_ATOM(warehouse_backend, dec_atom)

  // Used to increment the available count of an item.
  CAF_ADD_ATOM(warehouse_backend, inc_atom)

CAF_END_TYPE_ID_BLOCK(warehouse_backend)
