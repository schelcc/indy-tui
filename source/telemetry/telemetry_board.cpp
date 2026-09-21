#include <algorithm>
#include <cassert>
#include <execution>
#include <memory>
#include <mutex>
#include <ncpp/CellStyle.hh>
#include <ncpp/NCBox.hh>
#include <ncpp/NCKey.hh>
#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Root.hh>
#include <notcurses/ncseqs.h>
#include <notcurses/notcurses.h>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include "core.hpp"
#include "draw.hpp"
#include "locked.hpp"
#include "telemetry/driver_telemetry.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_frame.hpp"

#include "ErpMessage.pb.h"
#include "core/time.hpp"
#include "core/units.hpp"

#include "ui/ui.hpp"

using proto::telemetry::ErpMessage;

using Time::Clock;

namespace Telemetry {

TelemetryFrame::TelemetryFrame(std::string &&payload) noexcept
    : _msg(ErpMessage{}), _valid(false), _mod_time(Time::Clock::now()) {
  _msg.ParseFromString(payload);

  _valid = true;
}

void TelemetryBoard::reassociate_drivers() noexcept {
  // TODO: Figure out error handling
  std::shared_lock map_lock(_driver_map_mtx);
  {
    std::shared_lock vec_lock(_driver_vec_mtx);
    assert(_drivers.size() == _driver_map.size());
  }

  if (_drivers.empty())
    return;

  size_t idx{0};
  std::for_each(std::cbegin(_drivers), std::cend(_drivers),
                [this, &idx](DriverTelemetry const &d) {
                  assert(_driver_map.contains(d.get_car_num().data()));
                  _driver_map.at(d.get_car_num().data()) = idx++;
                });

  // Make sure nothing weird happened
  assert(idx == _drivers.size());
}

std::expected<void, TelemetryBoard::Err> TelemetryBoard::inform_new_frame(
    std::unique_ptr<TelemetryFrame> &&frame) noexcept {
  if (!frame->is_valid())
    return std::unexpected(Err(TelemetryBoard::Err::FRAME_INVALID));

  ErpMessage message{frame->take_from()};

  // Update event-wide information
  session_info.update(message);

  // Don't move on if we don't yet have track length or total checkpoints
  if (!session_info.num_checkpts.get_const()->has_value() ||
      session_info.lap_length.get_const()->has_value())
    return {};

  assert(session_info.num_checkpts.get_const()->value() > 0);

  // Check whether the driver is in the map, adding it if not. If the carnumber
  // couldn't be found, return false. If successful, return true. Can add in a
  // condition and guard initialized with short circuiting
  auto check_and_populate = [this](auto const &iter) {
    assert(iter.has_carnumber());

    std::string const &car_num = iter.carnumber();

    if (!_driver_map.contains(car_num)) {

      _driver_map[car_num] = _drivers.size();
      _drivers.emplace_back();

      auto &d = _drivers.back();

      d.set_checkpoints(session_info.num_checkpts.get_const()->value());
      d.set_lap_length(session_info.lap_length.get_const()->value());
    }

    return true;
  };

  std::unique_lock map_lock(_driver_map_mtx);
  std::unique_lock vec_lock(_driver_vec_mtx);

  if (message.telemetrymessages_size() > 0) {
    // Increment the stale count on all drivers, then we'll reset the ones we
    // come accross (only if we got any telemetry)
    std::for_each(std::begin(_drivers), std::end(_drivers),
                  [](DriverTelemetry &d) { d.frame_passed(); });

    std::for_each(message.mutable_telemetrymessages()->begin(),
                  message.mutable_telemetrymessages()->end(),
                  [this, &check_and_populate](auto iter) {
                    assert(check_and_populate(iter));

                    DriverTelemetry &driver =
                        _drivers.at(_driver_map.at(iter.carnumber()));

                    driver.take_new_telemetry(std::move(iter));
                  });
  }

  // Populate the driver table with the overall results, if included
  std::for_each(message.mutable_overallresults()->begin(),
                message.mutable_overallresults()->end(),
                [this, &check_and_populate](auto iter) {
                  assert(check_and_populate(iter));

                  _drivers.at(_driver_map.at(iter.carnumber()))
                      .take_new_results(std::move(iter));
                });

  // Populate the driver table with the completed lap information, if included
  std::for_each(message.mutable_completedlapresult()->begin(),
                message.mutable_completedlapresult()->end(),
                [this, &check_and_populate](auto iter) {
                  assert(check_and_populate(iter));

                  _drivers.at(_driver_map.at(iter.carnumber()))
                      .take_new_lap(std::move(iter));
                });

  // Driver objects complete each-lap updates here to keep as much out of the
  // above loops as possible
  std::for_each(std::execution::par_unseq, std::begin(_drivers),
                std::end(_drivers), [](auto &d) { d.refresh(); });

  return {};
}

ThreadSafe::LockPair<std::vector<DriverTelemetry> const &>
TelemetryBoard::reorder_and_get() {
  // Order drivers by rank
  {
    std::unique_lock lock(_driver_vec_mtx);
    std::sort(std::begin(_drivers), std::end(_drivers),
              DriverTelemetry::OrderByRank);
  }

  // We've changed the order of the drivers vec, so reassociate the LUT
  reassociate_drivers();

  // Calculate each driver's gap to leader
  {
    std::unique_lock lock(_driver_vec_mtx);
    if (_drivers.size() >= 2) {
      auto &leader = _drivers.front();
      size_t prev_idx = 0;
      std::for_each(std::begin(_drivers) + 1, std::end(_drivers),
                    [&leader, &prev_idx, this](DriverTelemetry &d) {
                      d.gap_to_leader =
                          d.diff_to_car(leader).value_or(d.gap_to_leader);
                      // d.calculate_gap(leader);
                      d.interval_to_next =
                          d.diff_to_car(_drivers.at(prev_idx++))
                              .value_or(d.interval_to_next);
                      // d.calculate_interval(_drivers.at(prev_idx++));
                    });
    }
  }

  // Acquire and return a shared_lock on the drivers vec
  return {std::shared_lock(_driver_vec_mtx), _drivers};
}

}; // namespace Telemetry
