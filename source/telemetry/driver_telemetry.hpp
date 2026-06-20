#pragma once

#include <expected>

#include "ErpMessage.pb.h"

namespace Telemetry {

class DriverTelemetry {
public:
  /** @brief Error type for any DriverTelemetry errors. */
  struct Err {
    enum Kind {
      MISSING_FIELD,
    } kind;
  };

  std::string _car_num = "";
  proto::telemetry::ErpTelemetry _telemetry{};
  proto::telemetry::ErpOverallResults _results{};
  proto::telemetry::ErpCompletedLapResult _completed_lap{};

  // Number of frames since we last got a telemetry update. Used to "invalidate"
  // stale telemetry
  size_t _frames_since_telem = 0;

  // Number of frames after which telemetry is considered stale
  static constexpr size_t STALE_TELEM_THRESH = 5;

public:
  std::atomic_bool in_pit = true;

  /** @brief Increment the telemetry stale count. */
  void frame_passed() { _frames_since_telem++; }

  /** @brief Check whether the current telemetry is valid (not stale). */
  bool is_telem_valid() const noexcept {
    return _frames_since_telem < STALE_TELEM_THRESH;
  }

  /** @brief Retrieve the driver's car number. */
  std::string_view const get_car_num() const noexcept { return _car_num; }

  /** @brief Take in the given telemetry. Resets telemetry stale count. */
  void take_new_telemetry(proto::telemetry::ErpTelemetry &&) noexcept;

  /** @brief Take in the given overall results. */
  void take_new_results(proto::telemetry::ErpOverallResults &&) noexcept;

  /** @brief Take in the given completed lap results. */
  void take_new_lap(proto::telemetry::ErpCompletedLapResult &&) noexcept;

  [[nodiscard]] int32_t get_rank() const;
  [[nodiscard]] std::string get_name() const;
  [[nodiscard]] double get_speed() const;

  // Sort options

  static bool OrderByRank(DriverTelemetry const &, DriverTelemetry const &);

  DriverTelemetry() = default;

  DriverTelemetry(DriverTelemetry const &) = delete;
  DriverTelemetry &operator=(DriverTelemetry const &) = delete;

  DriverTelemetry(DriverTelemetry &&) noexcept;
  DriverTelemetry &operator=(DriverTelemetry &&) noexcept;
};

}; // namespace Telemetry
