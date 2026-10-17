#include <algorithm>
#include <cassert>
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
#include <shared_mutex>
#include <string>
#include <string_view>

#include "locked.hpp"
#include "perf.hpp"
#include "telemetry/driver_telemetry.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_frame.hpp"

#include "ErpMessage.pb.h"
#include "core/time.hpp"
#include "core/units.hpp"

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
  std::unique_lock map_lock(_driver_map_mtx);
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
  if (!frame->is_valid()) [[unlikely]]
    return std::unexpected(Err(TelemetryBoard::Err::FRAME_INVALID));

  ErpMessage message{frame->take_from()};

  // Update event-wide information
  session_info.update(message);

  // Don't move on if we don't yet have track length or total checkpoints
  if (!session_info.num_checkpts.get_const()->has_value() ||
      !session_info.lap_length.get_const()->has_value()) [[unlikely]]
    return {};

  assert(session_info.num_checkpts.get_const()->value() > 0);

  // Check whether the driver is in the map, adding it if not. If the carnumber
  // couldn't be found, return false. If successful, return true. Can add in a
  // condition and guard initialized with short circuiting
  auto check_and_populate =
      [](auto const &iter, decltype(_driver_map) &map, decltype(_drivers) &vec,
         decltype(session_info) &sess_info) -> DriverTelemetry * {
    std::string_view car_num = iter->carnumber();

    if (!map.contains(car_num.data())) [[unlikely]] {
      map.emplace(car_num.data(), vec.size());
      vec.emplace_back();
      DriverTelemetry *out = &vec.back();
      out->set_checkpoints(sess_info.num_checkpts.get_const()->value());
      out->set_lap_length(sess_info.lap_length.get_const()->value());
      return out;
    } else [[likely]]
      return &vec.at(map.at(car_num.data()));
  };

  std::unique_lock map_lock(_driver_map_mtx);
  std::unique_lock vec_lock(_driver_vec_mtx);

  App::PerfContext::StartInformBoardTime();

  auto mut_telem = message.mutable_telemetrymessages();
  for (auto iter = mut_telem->begin(); iter < mut_telem->end(); ++iter)
    check_and_populate(iter, _driver_map, _drivers, session_info)
        ->take_new_telemetry(std::move(*iter));

  // Populate the driver table with the overall results, if included
  auto mut_overall = message.mutable_overallresults();
  for (auto iter = mut_overall->begin(); iter < mut_overall->end(); ++iter)
    check_and_populate(iter, _driver_map, _drivers, session_info)
        ->take_new_results(std::move(*iter));

  // Populate the driver table with the completed lap information, if included
  auto mut_lap = message.mutable_completedlapresult();
  for (auto iter = mut_lap->begin(); iter < mut_lap->end(); ++iter)
    check_and_populate(iter, _driver_map, _drivers, session_info)
        ->take_new_lap(std::move(*iter));

  // Driver objects complete each-lap updates here to keep as much out of the
  // above loops as possible
  for (auto &d : _drivers)
    d.refresh();

  App::PerfContext::StopInformBoardTime();

  return {};
}

ThreadSafe::LockPair<std::vector<DriverTelemetry> const>
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
    if (_drivers.size() >= 2) [[likely]] {
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
