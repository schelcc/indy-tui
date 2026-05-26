#include "telemetry/driver_telemetry.hpp"
#include "ErpMessage.pb.h"
#include <limits>

namespace Telemetry {

void DriverTelemetry::take_new_telemetry(
    proto::telemetry::ErpTelemetry &&telem) noexcept {
  _telemetry = std::move(telem);
  _car_num = _telemetry.carnumber();
  _frames_since_telem = 0;
}

void DriverTelemetry::take_new_results(
    proto::telemetry::ErpOverallResults &&results) noexcept {
  _results = std::move(results);
}

void DriverTelemetry::take_new_lap(
    proto::telemetry::ErpCompletedLapResult &&lap) noexcept {
  _completed_lap = std::move(lap);
}

[[nodiscard]] int32_t DriverTelemetry::get_rank() const {
  return _completed_lap.has_rank() ? _completed_lap.rank()
                                   : std::numeric_limits<int32_t>().max();
}

[[nodiscard]] std::string DriverTelemetry::get_name() const {
  return (_results.has_firstname() ? _results.firstname() : "Driver") + " " +
         (_results.has_lastname() ? _results.lastname() : "Name");
}

[[nodiscard]] double DriverTelemetry::get_speed() const {
  return (_telemetry.has_vehiclespeed() ? _telemetry.vehiclespeed() : 0.0);
}

[[nodiscard]] bool DriverTelemetry::OrderByRank(const DriverTelemetry &a,
                                                const DriverTelemetry &b) {
  return a.get_rank() < b.get_rank();
}

}; // namespace Telemetry
