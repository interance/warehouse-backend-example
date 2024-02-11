// (c) 2024, Interance GmbH & Co KG.

#include "ec.hpp"

namespace {

std::string_view ec_names[] = {
  "nil",
  "no_such_item",
  "key_already_exists",
};

} // namespace

std::string to_string(ec code) {
  return std::string{ec_names[static_cast<uint8_t>(code)]};
}

bool from_string(std::string_view name, ec& code) {
  for (size_t i = 0; i < static_cast<size_t>(ec::num_ec_codes); ++i) {
    if (name == ec_names[i]) {
      code = static_cast<ec>(i);
      return true;
    }
  }
  return false;
}

bool from_integer(uint8_t value, ec& code) {
  if (value < static_cast<uint8_t>(ec::num_ec_codes)) {
    code = static_cast<ec>(value);
    return true;
  }
  return false;
}
