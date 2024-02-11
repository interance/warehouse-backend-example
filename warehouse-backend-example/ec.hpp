// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include <caf/default_enum_inspect.hpp>
#include <caf/is_error_code_enum.hpp>

/// Application-specific error codes.
enum class ec : uint8_t {
  /// No error occurred.
  nil = 0,
  /// Indicates that a database query did not return any results.
  no_such_item,
  /// Indicates that a key already exists in the database.
  key_already_exists,
  /// Indicates that the database is not accessible.
  database_inaccessible,
  /// Indicates that a user-provided argument is invalid.
  invalid_argument,
  /// The number of error codes (must be last entry!).
  /// @note This value is not a valid error code.
  num_ec_codes,
};

/// @relates ec
std::string to_string(ec);

/// @relates ec
bool from_string(std::string_view, ec&);

/// @relates ec
bool from_integer(uint8_t, ec&);

/// @relates ec
template <class Inspector>
bool inspect(Inspector& f, ec& x) {
  return caf::default_enum_inspect(f, x);
}

CAF_ERROR_CODE_ENUM(ec)
