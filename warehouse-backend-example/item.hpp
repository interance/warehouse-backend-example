// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include <cstdint>
#include <string>

struct item {
  int32_t id;
  int32_t price;
  int32_t available;
  std::string name;
};

template <class Inspector>
bool inspect(Inspector& f, item& x) {
  return f.object(x).fields(f.field("id", x.id), f.field("price", x.price),
                            f.field("available", x.available),
                            f.field("name", x.name));
}

