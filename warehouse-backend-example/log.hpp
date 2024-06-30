// (c) 2024, Interance GmbH & Co KG.

// Convenience header for logging with a custom component name.

#pragma once

#include <caf/detail/build_config.hpp>
#include <caf/log/level.hpp>
#include <caf/logger.hpp>

#include <string_view>

namespace log {

/// The name of this component in log events.
constexpr std::string_view component = "app";

/// Logs a message with `debug` severity.
/// @param fmt_str The format string (with source location) for the message.
/// @param args Arguments for the format string.
template <class... Ts>
void debug(caf::format_string_with_location fmt_str, Ts&&... args) {
  caf::logger::log(caf::log::level::debug, component, fmt_str,
                   std::forward<Ts>(args)...);
}

/// Starts a new log event with `debug` severity.
inline auto debug() {
  return caf::logger::log(caf::log::level::debug, component);
}

/// Logs a message with `info` severity.
/// @param fmt_str The format string (with source location) for the message.
/// @param args Arguments for the format string.
template <class... Ts>
void info(caf::format_string_with_location fmt_str, Ts&&... args) {
  caf::logger::log(caf::log::level::info, component, fmt_str,
                   std::forward<Ts>(args)...);
}

/// Starts a new log event with `info` severity.
inline auto info() {
  return caf::logger::log(caf::log::level::info, component);
}

/// Logs a message with `warning` severity.
/// @param fmt_str The format string (with source location) for the message.
/// @param args Arguments for the format string.
template <class... Ts>
void warning(caf::format_string_with_location fmt_str, Ts&&... args) {
  caf::logger::log(caf::log::level::warning, component, fmt_str,
                   std::forward<Ts>(args)...);
}

/// Starts a new log event with `warning` severity.
inline auto warning() {
  return caf::logger::log(caf::log::level::warning, component);
}

/// Logs a message with `error` severity.
/// @param fmt_str The format string (with source location) for the message.
/// @param args Arguments for the format string.
template <class... Ts>
void error(caf::format_string_with_location fmt_str, Ts&&... args) {
  caf::logger::log(caf::log::level::error, component, fmt_str,
                   std::forward<Ts>(args)...);
}

/// Starts a new log event with `error` severity.
inline auto error() {
  return caf::logger::log(caf::log::level::error, component);
}

} // namespace log
