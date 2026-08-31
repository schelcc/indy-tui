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

void DriverTelemetry::take_new_crossing(
    proto::telemetry::ErpLineCrossingMessage &&crossing) noexcept {
  {
    std::unique_lock lock(_line_crossings_mtx);
    _line_crossings[crossing.timeline()] = crossing.elapsedtime();
  }
  {
    std::unique_lock lock(_last_line_crossing_mtx);
    _last_line_crossing = crossing.timeline();
  }
}

void DriverTelemetry::calculate_gap(DriverTelemetry &leader) noexcept {
  std::string checked_timeline;
  {
    std::shared_lock lock(_last_line_crossing_mtx);
    if (!_last_line_crossing.has_value())
      return;
    checked_timeline = _last_line_crossing.value();
  }

  {
    std::shared_lock others_lock(leader._line_crossings_mtx);
    std::shared_lock ours_lock(_line_crossings_mtx);

    auto others_it = leader._line_crossings.find(checked_timeline);
    if (others_it == leader._line_crossings.end()) {
      _gap.store(-99);
      return;
    }

    auto ours_it = _line_crossings.find(checked_timeline);
    if (ours_it == _line_crossings.end()) {
      _gap.store(-99);
      return;
    }

    _gap.store((others_it->second - ours_it->second) / 10000.0);
  }
}

void DriverTelemetry::calculate_interval(DriverTelemetry &next) noexcept {
  std::string checked_timeline;
  {
    std::shared_lock lock(_last_line_crossing_mtx);
    if (!_last_line_crossing.has_value())
      return;
    checked_timeline = _last_line_crossing.value();
  }

  {
    std::shared_lock others_lock(next._line_crossings_mtx);
    std::shared_lock ours_lock(_line_crossings_mtx);

    auto others_it = next._line_crossings.find(checked_timeline);
    if (others_it == next._line_crossings.end()) {
      _interval.store(-99);
      return;
    }

    auto ours_it = _line_crossings.find(checked_timeline);
    if (ours_it == _line_crossings.end()) {
      _interval.store(-99);
      return;
    }

    _interval.store((others_it->second - ours_it->second) / 10000.0);
  }
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

DriverTelemetry::DriverTelemetry(DriverTelemetry &&d) noexcept {
  _car_num = d._car_num;
  _telemetry = std::move(d._telemetry);
  _results = std::move(d._results);
  _completed_lap = std::move(d._completed_lap);
  _frames_since_telem = d._frames_since_telem;
  in_pit = d.in_pit.load();
}

DriverTelemetry &DriverTelemetry::operator=(DriverTelemetry &&d) noexcept {
  _car_num = d._car_num;
  _telemetry = std::move(d._telemetry);
  _results = std::move(d._results);
  _completed_lap = std::move(d._completed_lap);
  _frames_since_telem = d._frames_since_telem;
  in_pit = d.in_pit.load();
  return *this;
}

}; // namespace Telemetry
