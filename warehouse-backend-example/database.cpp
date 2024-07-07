// (c) 2024, Interance GmbH & Co KG.

#include "database.hpp"

#include <caf/sec.hpp>

#include <sqlite3.h>

database::~database() {
  if (db_ != nullptr)
    sqlite3_close(db_);
}

caf::error database::open() {
  // Open the database file.
  if (sqlite3_open(db_file_.c_str(), &db_) != SQLITE_OK)
    return make_error(caf::sec::runtime_error, "could not open database");
  // Create the table if it does not exist.
  const char* create_table = "CREATE TABLE IF NOT EXISTS items ("
                             "id INTEGER PRIMARY KEY,"
                             "name TEXT NOT NULL,"
                             "price INTEGER NOT NULL,"
                             "available INTEGER NOT NULL)";
  char* err_msg = nullptr;
  if (sqlite3_exec(db_, create_table, nullptr, nullptr, &err_msg)
      != SQLITE_OK) {
    auto msg = std::string{err_msg};
    sqlite3_free(err_msg);
    return make_error(caf::sec::runtime_error, std::move(msg));
  }
  return caf::error{};
}

int database::count() {
  const char* count_query = "SELECT COUNT(*) FROM items";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, count_query, -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return 0;
  }
  auto result = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return result;
}

std::optional<item> database::get(int32_t id) {
  const char* get_query = R"_(
    SELECT id, name, price, available
    FROM items WHERE id = ?
  )_";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, get_query, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  if (sqlite3_bind_int(stmt, 1, id) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  item result;
  result.id = sqlite3_column_int(stmt, 0);
  result.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  result.price = sqlite3_column_int(stmt, 2);
  result.available = sqlite3_column_int(stmt, 3);
  sqlite3_finalize(stmt);
  return result;
}

ec database::insert(const item& new_item) {
  const char* insert_query = R"_(
    INSERT INTO items (id, name, price, available)
    VALUES (?, ?, ?, ?)
  )_";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, insert_query, -1, &stmt, nullptr) != SQLITE_OK)
    return ec::database_inaccessible;
  if (sqlite3_bind_int(stmt, 1, new_item.id) != SQLITE_OK
      || sqlite3_bind_text(stmt, 2, new_item.name.c_str(), -1, SQLITE_STATIC)
           != SQLITE_OK
      || sqlite3_bind_int(stmt, 3, new_item.price) != SQLITE_OK
      || sqlite3_bind_int(stmt, 4, new_item.available) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return ec::database_inaccessible;
  }
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return ec::key_already_exists;
  }
  sqlite3_finalize(stmt);
  return ec::nil;
}

ec database::inc(int32_t id, int32_t amount) {
  if (amount <= 0)
    return ec::invalid_argument;
  const char* inc_query = R"_(
    UPDATE items
    SET available = available + ?
    WHERE id = ?
  )_";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, inc_query, -1, &stmt, nullptr) != SQLITE_OK)
    return ec::database_inaccessible;
  if (sqlite3_bind_int(stmt, 1, amount) != SQLITE_OK
      || sqlite3_bind_int(stmt, 2, id) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return ec::database_inaccessible;
  }
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return ec::no_such_item;
  }
  sqlite3_finalize(stmt);
  return ec::nil;
}

ec database::dec(int32_t id, int32_t amount) {
  if (amount <= 0)
    return ec::invalid_argument;
  // Decrement `amount` but never go below 0.
  const char* dec_query = R"_(
    UPDATE items
    SET available = CASE WHEN available < ? THEN 0 ELSE available - ? END
    WHERE id = ?
  )_";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, dec_query, -1, &stmt, nullptr) != SQLITE_OK)
    return ec::database_inaccessible;
  if (sqlite3_bind_int(stmt, 1, amount) != SQLITE_OK
      || sqlite3_bind_int(stmt, 2, amount) != SQLITE_OK
      || sqlite3_bind_int(stmt, 3, id) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return ec::database_inaccessible;
  }
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return ec::no_such_item;
  }
  sqlite3_finalize(stmt);
  return ec::nil;
}

ec database::del(int32_t id) {
  const char* del_query = "DELETE FROM items WHERE id = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, del_query, -1, &stmt, nullptr) != SQLITE_OK)
    return ec::database_inaccessible;
  if (sqlite3_bind_int(stmt, 1, id) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return ec::database_inaccessible;
  }
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return ec::no_such_item;
  }
  sqlite3_finalize(stmt);
  return ec::nil;
}
