#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <ncpp/Plane.hh>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "google/protobuf/repeated_ptr_field.h"

namespace Telemetry {

// Forward decl.
class DriverTelemetry;
class TelemetryFrame;

/** @brief Represents the collection of telemetry composing the leaderboard. */
class TelemetryBoard {
public:
  /** @brief Error type for any TelemetryBoard failures. */
  struct Err {
    enum Kind { FRAME_INVALID, MISSING_TELEM, MISSING_RES, MISSING_LAP } kind;
  };

private:
  template <typename Field>
  using ProtoIter = google::protobuf::RepeatedPtrField<Field>;

  std::shared_mutex _driver_vec_mtx;
  std::shared_mutex _driver_map_mtx;

  std::vector<DriverTelemetry> _drivers;
  std::unordered_map<std::string, size_t> _driver_map{};

  /** @brief Re-match the car-number to driver indices in the driver map. */
  void reassociate_drivers() noexcept;

public:
  /** @brief Fill in newly received telemetry information. Returns an expected
   * with the error encountered, if any. */
  std::expected<void, Err>
  inform_new_frame(std::unique_ptr<TelemetryFrame> &&) noexcept;

  /** @brief Draw a basic leaderboard noting the drivers' rank, name, number,
   * and current speed. */
  void draw_basic(std::shared_ptr<ncpp::Plane>, int &start_row);
};

}; // namespace Telemetry
