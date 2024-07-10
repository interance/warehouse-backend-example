// (c) 2024, Interance GmbH & Co KG.

#pragma once

#include "ec.hpp"
#include "item.hpp"

#include <caf/error.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

extern "C" {

struct sqlite3;

} // extern "C"

/// A simple database interface for storing items.
class database {
public:
  database(std::string db_file) : db_file_(std::move(db_file)) {
    // nop
  }

  ~database();

  /// Opens the database file and creates the table if it does not exist.
  /// @returns `caf::error{}` on success, an error code otherwise.
  [[nodiscard]] caf::error open();

  /// Retrieves the number of items in the database.
  /// @returns the number of items in the database.
  [[nodiscard]] int count();

  /// Retrieves an item from the database.
  /// @returns the item if found, `std::nullopt` otherwise.
  [[nodiscard]] std::optional<item> get(int32_t id);

  /// Inserts a new item into the database.
  /// @returns `ec::nil` on success, an error code otherwise.
  [[nodiscard]] ec insert(const item& new_item);

  /// Increments the available count of an item.
  /// @returns `ec::nil` on success, an error code otherwise.
  [[nodiscard]] ec inc(int32_t id, int32_t amount);

  /// Decrements the available count of an item.
  /// @returns `ec::nil` on success, an error code otherwise.
  [[nodiscard]] ec dec(int32_t id, int32_t amount);

  /// Deletes an item from the database.
  /// @returns `ec::nil` on success, an error code otherwise.
  [[nodiscard]] ec del(int32_t id);

private:
  std::string db_file_;
  sqlite3* db_ = nullptr;
};

/// A smart pointer to an item database.
using database_ptr = std::shared_ptr<database>;
