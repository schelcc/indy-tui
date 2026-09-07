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
  {
    std::unique_lock lock(_event_info.mtx);

    // TrackInformation fields
    if (message.trackinformation_size() > 0) {
      proto::telemetry::ErpTrackInformation const &info =
          message.trackinformation().Get(0);

      if (info.has_trackname())
        _event_info.track_name = info.trackname();

      // Only do once
      if (info.has_tracklength() && !_event_info.num_checkpts.has_value())
        // We get tracklength as miles, so convert to meters and then calculate
        // the number of checkpoints
        _event_info.num_checkpts = static_cast<size_t>(
            std::ceil((info.tracklength() * Units::METERS_PER_MILE) /
                      DriverTelemetry::CHECKPOINT_DIST));

      if (info.has_tracktype())
        _event_info.track_type = info.tracktype();
    }

    // HeartBeat fields
    if (message.heartbeats_size() > 0) {
      proto::telemetry::ErpHeartBeat const &hbeat = message.heartbeats().Get(0);

      if (hbeat.has_eventname())
        _event_info.event_name = hbeat.eventname();

      if (hbeat.has_series())
        _event_info.series_name = hbeat.series();

      if (hbeat.has_currentflag())
        _event_info.flag_status = hbeat.currentflag();

      if (hbeat.has_sessiontype())
        _event_info.session_type = hbeat.sessiontype();

      if (hbeat.has_sessionstatus())
        _event_info.session_status = hbeat.sessionstatus();

      if (hbeat.has_overalltimetogo())
        _event_info.time_to_go = hbeat.overalltimetogo();

      if (hbeat.has_timeofday())
        _event_info.track_time = hbeat.timeofday();

      if (hbeat.has_completedlaps())
        _event_info.completed_laps = hbeat.completedlaps();

      if (hbeat.has_totallaps())
        _event_info.total_laps = hbeat.totallaps();

      if (hbeat.has_tracklength())
        _event_info.lap_length =
            std::stod(hbeat.tracklength()) * Units::METERS_PER_MILE;
    }
  }

  // Don't move on if we don't yet have track length
  if (!_event_info.num_checkpts.has_value() ||
      !_event_info.lap_length.has_value())
    return {};

  assert(_event_info.num_checkpts.value() > 0);

  // Check whether the driver is in the map, adding it if not. If the carnumber
  // couldn't be found, return false. If successful, return true. Can add in a
  // condition and guard initialized with short circuiting
  auto check_and_populate = [this](auto const &iter) {
    assert(iter.has_carnumber());

    std::string const &car_num = iter.carnumber();

    if (!_driver_map.contains(car_num)) {

      _driver_map[car_num] = _drivers.size();
      _drivers.emplace_back();
      _drivers.back().set_checkpoints(_event_info.num_checkpts.value());
      _drivers.back().set_lap_length(_event_info.lap_length.value());
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

void TelemetryBoard::draw_columns() {

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
    std::shared_lock lock(_driver_vec_mtx);
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

  std::shared_lock lock(_column_planes_mtx);
  auto v = std::views::zip(_column_planes, _columns);
  std::for_each(
      std::execution::par, std::begin(v), std::end(v),
      [&, this](auto plane_col_pair) -> void {
        ColumnPlane &col_plane = std::get<0>(plane_col_pair);
        ColumnPair &col_pair = std::get<1>(plane_col_pair);

        std::scoped_lock col_lock(col_plane.mtx);
        std::shared_lock driver_lock(_driver_vec_mtx);

        auto &plane = col_plane.plane;
        auto &func = col_pair.func;
        auto &name = col_pair.name;

        plane->erase();
        Tools::Draw::border_with_title(plane, name.data());

        size_t row = 1;

        std::for_each(
            std::cbegin(_drivers), std::cend(_drivers),
            [func, &row, &plane](auto const &driver) {
              std::invoke(func, driver).apply_to_plane(plane, row++, 1);
            });
      });
}

void TelemetryBoard::draw_event_info(std::shared_ptr<ncpp::Plane> plane) {
  size_t row = 0;

  // TODO: Add putstr helper to Tools::Draw for stylized text

  // FIX: Gonna start with a very simplistic event info, add to later for
  // event-specific things (like split group practices and qualifying)

  // FIX: Figure out what to do given row count

  // TODO: Might be good to break the row names and fields into two different
  // planes to simplify later styling & alignment
  plane->perimeter_rounded(ncpp::NCBox::CornerMask, plane->get_channels(), 0);

  std::shared_lock lock(_event_info.mtx);

  using Tools::Draw::trunc_str;

  UI::SimpleTableSketcher put_simple_field(plane, row);

  plane->putstr(row++, 1, "Event Information");

  put_simple_field("Event Name", _event_info.event_name);
  put_simple_field("Track Name", _event_info.track_name);
  put_simple_field("Session Type", _event_info.session_type);
  put_simple_field("Session Status", _event_info.session_status);
  put_simple_field("Track Time", _event_info.track_time);

  UI::Color flag_color = UI::Color::NONE;
  if (_event_info.flag_status.has_value()) {
    std::string &cur_flag = _event_info.flag_status.value();
    if (cur_flag == "GREEN")
      flag_color = UI::Color::GREEN_SOFT;
    else if (cur_flag == "YELLOW")
      flag_color = UI::Color::YELLOW_SOFT;
    else if (cur_flag == "RED")
      flag_color = UI::Color::RED_SOFT;
    else if (cur_flag == "WARM")
      flag_color = UI::Color::PURPLE_SOFT;
  }

  put_simple_field("Flag Status",
                   UI::String(_event_info.flag_status.value_or("--"),
                              flag_color, UI::Style::BOLD) +
                       "         ");

  put_simple_field(
      "Laps",
      std::format(
          "{} / {}",
          _event_info.completed_laps.has_value()
              ? std::format("{}", _event_info.completed_laps.value() + 1)
              : "--",
          _event_info.total_laps.has_value()
              ? std::format("{}", _event_info.total_laps.value())
              : "--"));
}
}; // namespace Telemetry
