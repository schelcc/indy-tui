#pragma once

#include <limits>
#include <shared_mutex>

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

  struct Checkpoint {
    std::optional<size_t> time = {};
    size_t age = 0;
  };

  std::string _car_num = "";

  proto::telemetry::ErpTelemetry _telemetry{};
  std::atomic_bool _new_telem = false;

  proto::telemetry::ErpOverallResults _results{};
  std::atomic_bool _new_results = false;

  proto::telemetry::ErpCompletedLapResult _completed_lap{};
  std::atomic_bool _new_lap = false;

  std::atomic_bool _timing_debounce = false;

  std::atomic_size_t _cur_checkpt = 0;

  mutable std::shared_mutex _last_checkpt_mtx;
  std::optional<size_t> _last_checkpt = {};

  std::vector<Checkpoint> _dist_times{};
  mutable std::shared_mutex _dist_mtx;

  // Number of frames since we last got a telemetry update. Used to "invalidate"
  // stale telemetry. Start past the threshold, as we are by default invalid.
  size_t _frames_since_telem = STALE_TELEM_THRESH + 1;

  std::atomic<double> _lap_length = std::numeric_limits<double>::max();

  // Fraction of total lap distance after which distance reset is available
  static constexpr double LAP_DIST_RESET_PCT = 0.97;

  // Number of frames after which telemetry is considered stale
  static constexpr size_t STALE_TELEM_THRESH = 5;

  // After what distance should we consider a checkpoint
  static constexpr long CHECKPOINT_DIST = 100;

  // How far past the distance checkpoints can we be before we are no longer
  // considered at the checkpoint
  static constexpr long CHECKPOINT_DIST_THRESH = 15;

  // For some reason, the telemetry timestamp randomly increases by
  // 2e6 or more and then comes back down, so for checkpoint
  // calculations we'll need to skip these erroneous timestamps
  static constexpr size_t MAX_TIMESTAMP_DIFF_ALLOWED = 1e6;

public:
  static constexpr size_t MIN_IN_PIT_CNT = 25;

  std::atomic_size_t in_pit_count = 0;
  std::atomic_bool in_pit = true;

  std::atomic<double> gap_to_leader = 0.0;
  std::atomic<double> interval_to_next = 0.0;

  std::atomic_size_t cur_laps = 0;

  /** @brief Increment the telemetry stale count. */
  void frame_passed() { _frames_since_telem++; }

  /** @brief Check whether the current telemetry is valid (not stale). */
  bool is_telem_valid() const noexcept {
    return _frames_since_telem <= STALE_TELEM_THRESH;
  }

  /** @brief Retrieve the driver's car number. */
  std::string_view const get_car_num() const noexcept { return _car_num; }

  /** @brief Take in the given telemetry. Resets telemetry stale count. */
  void take_new_telemetry(proto::telemetry::ErpTelemetry &&) noexcept;

  /** @brief Take in the given overall results. */
  void take_new_results(proto::telemetry::ErpOverallResults &&) noexcept;

  /** @brief Take in the given completed lap results. */
  void take_new_lap(proto::telemetry::ErpCompletedLapResult &&) noexcept;

  /** @brief Perform each-lap updates for each telemetry type. */
  void refresh() noexcept;

  /** @brief Configure the number of checkpoints which should be available. */
  void set_checkpoints(size_t const) noexcept;

  /** @brief Configure the total lap length in meters. */
  void set_lap_length(double const) noexcept;

  /** @brief Retrieve the last time this driver passed the given checkpoint
   * index, if available. */
  [[nodiscard]] std::optional<Checkpoint> get_checkpoint(size_t const) const;

  /** @brief Calculate the time difference between this and the given cars,
   * returning the time in sec if available. */
  std::optional<double> diff_to_car(DriverTelemetry const &) const noexcept;

  [[nodiscard]] int32_t get_rank() const;
  [[nodiscard]] std::string get_name() const;
  [[nodiscard]] double get_speed() const;
  [[nodiscard]] std::optional<size_t> get_last_checkpt() const;

  // Sort options

  static bool OrderByRank(DriverTelemetry const &, DriverTelemetry const &);
  static bool OrderByDist(DriverTelemetry const &, DriverTelemetry const &);
  static bool OrderByLapsDown(DriverTelemetry const &, DriverTelemetry const &);

  DriverTelemetry() = default;

  DriverTelemetry(DriverTelemetry const &) = delete;
  DriverTelemetry &operator=(DriverTelemetry const &) = delete;

  DriverTelemetry(DriverTelemetry &&) noexcept;
  DriverTelemetry &operator=(DriverTelemetry &&) noexcept;
};

}; // namespace Telemetry
