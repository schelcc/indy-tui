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

  std::string _car_num;
  proto::telemetry::ErpTelemetry _telemetry{};
  proto::telemetry::ErpOverallResults _results{};
  proto::telemetry::ErpCompletedLapResult _completed_lap{};

  // Number of frames since we last got a telemetry update. Used to "invalidate"
  // stale telemetry
  size_t _frames_since_telem{0};

  // Number of frames after which telemetry is considered stale
  static constexpr size_t STALE_TELEM_THRESH = 5;

public:
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

  // /** @brief Retrieve a reference to the underlying telemetry, if available.
  // */
  // [[nodiscard]] std::expected<
  //     std::reference_wrapper<const proto::telemetry::ErpTelemetry>, Err>
  //     const
  // get_telemetry_ref() const {
  //   // return (_telemetry.IsInitialized() &&
  //   !_telemetry.IsInitializedWithErrors())
  //   //            ? std::expected<std::reference_wrapper<
  //   //                                const proto::telemetry::ErpTelemetry>,
  //   //                            Err>(std::cref(_telemetry))
  //   //            : std::unexpected(Err(Err::MISSING_FIELD));
  // }

  // /** @brief Retrieve a reference to the underlying overall results, if
  //  * available. */
  // [[nodiscard]] std::expected<
  //     std::reference_wrapper<const proto::telemetry::ErpOverallResults>,
  //     Err> const
  // get_results_ref() const {
  //   return (_telemetry.IsInitialized() &&
  //   !_telemetry.IsInitializedWithErrors())
  //              ? std::expected<std::reference_wrapper<
  //                                  const
  //                                  proto::telemetry::ErpOverallResults>,
  //                              Err>(std::cref(_results))
  //              : std::unexpected(Err(Err::MISSING_FIELD));
  // }

  // /** @brief Retrieve a reference to the underlying lap completion stats, if
  //  * available. */
  // [[nodiscard]] std::expected<
  //     std::reference_wrapper<const proto::telemetry::ErpCompletedLapResult>,
  //     Err> const
  // get_lap_completed_ref() const {
  //   return (_telemetry.IsInitialized() &&
  //   !_telemetry.IsInitializedWithErrors())
  //              ? std::expected<
  //                    std::reference_wrapper<
  //                        const proto::telemetry::ErpCompletedLapResult>,
  //                    Err>(std::cref(_completed_lap))
  //              : std::unexpected(Err(Err::MISSING_FIELD));
  // }

  [[nodiscard]] int32_t get_rank() const;
  [[nodiscard]] std::string get_name() const;
  [[nodiscard]] double get_speed() const;

  // Sort options

  static bool OrderByRank(DriverTelemetry const &, DriverTelemetry const &);
};

}; // namespace Telemetry
