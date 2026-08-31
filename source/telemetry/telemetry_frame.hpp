#pragma once

#include <expected>
#include <functional>
#include <type_traits>
#include <vector>

#include "ErpMessage.pb.h"
#include "time.hpp"

namespace Telemetry {

/** @brief Represents a single instance of received telemetry. Not thread-safe,
 * proper locking must be employed by the user.  */
class TelemetryFrame {
  proto::telemetry::ErpMessage _msg{};

  bool _valid{false};

  Time::TimePoint _mod_time;

public:
  /** @brief Determine if this frame is valid. */
  [[nodiscard]] bool is_valid() const { return _valid; }

  /** @brief Retrieve the time this frame was last created or replaced. */
  [[nodiscard]] Time::TimePoint get_modtime() const { return _mod_time; }

  /** @brief Move out the underlying message for callers to use. */
  [[nodiscard]] proto::telemetry::ErpMessage take_from() noexcept {
    // Non-pessimizing -- we are moving out the message
    _valid = false;
    return std::move(_msg);
  }

  TelemetryFrame(std::string &&) noexcept;
};

}; // namespace Telemetry
