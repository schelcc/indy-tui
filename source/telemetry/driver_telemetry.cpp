#include "telemetry/driver_telemetry.hpp"
#include "ErpMessage.pb.h"
#include <cstdlib>
#include <execution>
#include <limits>
#include <mutex>
#include <shared_mutex>

namespace Telemetry {

void DriverTelemetry::take_new_telemetry(
    proto::telemetry::ErpTelemetry &&telem) noexcept {
  // If decreasing lap distance, we must be above the allowed fraction of the
  // total lap length, so if it's at or below the threshold we override the
  // value with the previous one
  if (_telemetry.has_lapdistance() &&
      (telem.lapdistance() < _telemetry.lapdistance()) &&
      (_telemetry.lapdistance() <= (_lap_length.load() * LAP_DIST_RESET_PCT)))
    telem.set_lapdistance(_telemetry.lapdistance());

  // Decreasing lap distance => Current lap distance is above reset threshold
  assert(
      !(_telemetry.lapdistance() > telem.lapdistance()) ||
      (_telemetry.lapdistance() >= (_lap_length.load() * LAP_DIST_RESET_PCT)));

  // Take in new telemetry
  _telemetry = std::move(telem);
  _car_num = _telemetry.carnumber();
  _frames_since_telem = 0;
  _new_telem = true;
}

void DriverTelemetry::take_new_results(
    proto::telemetry::ErpOverallResults &&results) noexcept {
  _results = std::move(results);
  _new_results = true;
}

void DriverTelemetry::take_new_lap(
    proto::telemetry::ErpCompletedLapResult &&lap) noexcept {
  _completed_lap = std::move(lap);
  _new_lap = true;
}

void DriverTelemetry::refresh() noexcept {
  if (_new_telem) {
    // Pit-status update
    if (_telemetry.has_isinpit()) {
      in_pit_count = (_telemetry.isinpit() ? in_pit_count + 1 : 0);

      in_pit = in_pit_count >= MIN_IN_PIT_CNT;
    } else {
      in_pit_count = 0;
      in_pit = false;
    }

    // Checkpoint update
    if (_telemetry.has_lapdistance()) {
      auto checkpt = std::div(static_cast<long>(_telemetry.lapdistance()),
                              CHECKPOINT_DIST);

      if (static_cast<size_t>(checkpt.quot) != _cur_checkpt.load()) {
        std::unique_lock lock(_dist_mtx);
        assert(_dist_times.size() > static_cast<size_t>(checkpt.quot));

        Checkpoint &prec_checkpt =
            _dist_times.at((checkpt.quot - 1) % _dist_times.size());
        Checkpoint &this_checkpt = _dist_times.at(checkpt.quot);

        size_t new_timestamp = _telemetry.timeofday();

        if (prec_checkpt.time.has_value()) {
          size_t prec_timestamp = prec_checkpt.time.value();
          // If the preceeding timestamp is significantly larger than this one,
          // we clear it and populate this one
          if (prec_timestamp > (new_timestamp + MAX_TIMESTAMP_DIFF_ALLOWED)) {
            prec_checkpt.time = {};
            prec_checkpt.age = 0;

            this_checkpt.time = new_timestamp;
            this_checkpt.age = 0;
          } else if (new_timestamp >
                     (prec_timestamp + MAX_TIMESTAMP_DIFF_ALLOWED)) {
            // If the new timestamp is significantly larger than the preceeding
            // one, clear this one
            this_checkpt.time = {};
            this_checkpt.age = 0;
          } else {
            // This and the preceeding timestamp are likely valid
            this_checkpt.time = new_timestamp;
            this_checkpt.age = 0;
          }
        } else {
          // Preceeding checkpoint is empty, so populate this one and move on
          this_checkpt.time = new_timestamp;
          this_checkpt.age = 0;
        }

        std::unique_lock checkpt_lock(_last_checkpt_mtx);
        _last_checkpt = _cur_checkpt;
      }

      _cur_checkpt.store(checkpt.quot);
    }

    _new_telem = false;
  }

  // if (_new_results) {

  //   _new_results = false;
  // }

  // if (_new_lap) {

  //   _new_lap = false;
  // }

  // Do at the start of each lap
  if (_results.has_laps() &&
      (static_cast<size_t>(_results.laps()) != cur_laps.load())) {
    // Checkpoint refresh
    {
      std::unique_lock lock(_dist_mtx);
      std::for_each(std::execution::par_unseq, std::begin(_dist_times),
                    std::end(_dist_times), [](auto &c) {
                      if (c.time.has_value())
                        c.age++;
                    });
    }

    cur_laps = static_cast<size_t>(_results.laps());
  }
}

std::optional<double>
DriverTelemetry::diff_to_car(DriverTelemetry const &d) const noexcept {
  std::shared_lock lock(_last_checkpt_mtx);
  if (!_last_checkpt.has_value())
    return {};

  auto other_timestamp = d.get_checkpoint(_last_checkpt.value());
  auto our_timestamp = get_checkpoint(_last_checkpt.value());

  if (!other_timestamp.has_value() || !our_timestamp.has_value())
    return {};

  auto const &other_ts = other_timestamp.value();
  auto const &our_ts = our_timestamp.value();

  if (!other_ts.time.has_value() || !our_ts.time.has_value())
    return {};

  assert(other_ts.time.value() > 0);
  assert(our_ts.time.value() > 0);

  if (std::abs(static_cast<int>(other_ts.age) - static_cast<int>(our_ts.age)) >
      1)
    return {};

  double diff = (static_cast<int>(other_ts.time.value()) -
                 static_cast<int>(our_ts.time.value())) /
                1000.0;

  return diff;
}

void DriverTelemetry::set_checkpoints(size_t const num) noexcept {
  std::unique_lock lock(_dist_mtx);
  std::optional<size_t> v = {};
  assert(!v.has_value());
  _dist_times = std::vector<Checkpoint>(num, Checkpoint{v, 0});
}

void DriverTelemetry::set_lap_length(double const length_meters) noexcept {
  assert(length_meters > 0);
  _lap_length = length_meters;
}

[[nodiscard]] std::optional<DriverTelemetry::Checkpoint>
DriverTelemetry::get_checkpoint(size_t const checkpt) const {
  std::shared_lock lock(_dist_mtx);
  if (_dist_times.size() <= checkpt)
    return {};
  return _dist_times.at(checkpt);
}

[[nodiscard]] int32_t DriverTelemetry::get_rank() const {
  return _results.has_overallrank() ? _results.overallrank()
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

[[nodiscard]] bool DriverTelemetry::OrderByDist(const DriverTelemetry &a,
                                                const DriverTelemetry &b) {
  return a._telemetry.lapdistance() < b._telemetry.lapdistance();
}

[[nodiscard]] bool DriverTelemetry::OrderByLapsDown(const DriverTelemetry &a,
                                                    const DriverTelemetry &b) {
  return a._completed_lap.lapsbehindleader() <
         b._completed_lap.lapsbehindleader();
}

DriverTelemetry::DriverTelemetry(DriverTelemetry &&d) noexcept {
  std::unique_lock dist_lock(_dist_mtx);
  std::unique_lock chekpt_lock(_last_checkpt_mtx);
  _car_num = d._car_num;
  _telemetry = std::move(d._telemetry);
  _results = std::move(d._results);
  _completed_lap = std::move(d._completed_lap);
  _frames_since_telem = d._frames_since_telem;
  in_pit = d.in_pit.load();
  _dist_times = std::move(d._dist_times);
  _last_checkpt = std::move(d._last_checkpt);
  _cur_checkpt.store(d._cur_checkpt.load());
  _lap_length.store(d._lap_length);
}

DriverTelemetry &DriverTelemetry::operator=(DriverTelemetry &&d) noexcept {
  std::unique_lock dist_lock(_dist_mtx);
  std::unique_lock chekpt_lock(_last_checkpt_mtx);
  _car_num = d._car_num;
  _telemetry = std::move(d._telemetry);
  _results = std::move(d._results);
  _completed_lap = std::move(d._completed_lap);
  _frames_since_telem = d._frames_since_telem;
  in_pit = d.in_pit.load();
  _dist_times = std::move(d._dist_times);
  _last_checkpt = std::move(d._last_checkpt);
  _cur_checkpt.store(d._cur_checkpt.load());
  _lap_length.store(d._lap_length);
  return *this;
}

}; // namespace Telemetry
