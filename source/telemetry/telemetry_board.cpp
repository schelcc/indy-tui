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

#include "draw.hpp"
#include "telemetry/driver_telemetry.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_frame.hpp"

#include "ErpMessage.pb.h"
#include "time.hpp"

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
    }
  }

  // Check whether the driver is in the map, adding it if not. If the carnumber
  // couldn't be found, return false. If successful, return true. Can add in a
  // condition and guard initialized with short circuiting
  auto check_and_populate = [this](auto const &iter) {
    assert(iter.has_carnumber());

    std::string const &car_num = iter.carnumber();

    if (!_driver_map.contains(car_num)) {
      _driver_map[car_num] = _drivers.size();
      _drivers.emplace_back(DriverTelemetry{});
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

                    _drivers.at(_driver_map.at(iter.carnumber()))
                        .take_new_telemetry(std::move(iter));
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

  std::shared_lock lock(_column_planes_mtx);
  auto v = std::views::zip(_column_planes, _columns);
  std::for_each(std::execution::par, std::begin(v), std::end(v),
                [&, this](auto plane_col_pair) -> void {
                  ColumnPlane &col_plane = std::get<0>(plane_col_pair);
                  ColumnPair &col_pair = std::get<1>(plane_col_pair);

                  std::scoped_lock col_lock(col_plane.mtx);
                  std::shared_lock driver_lock(_driver_vec_mtx);

                  size_t idx = 0;

                  auto &plane = col_plane.plane;
                  auto &func = col_pair.func;
                  auto &name = col_pair.name;

                  plane->perimeter_rounded(ncpp::NCBox::CornerMask,
                                           plane->get_channels(), 0);

                  plane->putstr(idx++, 1, name.data());

                  std::for_each(std::cbegin(_drivers), std::cend(_drivers),
                                [this, func, &idx, &plane](auto const &driver) {
                                  func(idx++, driver, plane);
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

  // Add line for the given field w/ the fstr "<field>: {}"
  auto put_simple_field =
      [&plane,
       &row](std::string_view const field_name,
             Core::IsOneOf<std::string, std::optional<std::string>> auto const
                 &field) -> void {
    std::string field_val{""};

    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(field)>,
                                 std::string>) {
      field_val = field;
    } else {
      field_val = field.value_or("--");
    }

    plane->putstr(row++, 1,
                  trunc_str(std::format("{}: {}", field_name, field_val),
                            plane->get_dim_x() - 2)
                      .data());
  };

  plane->putstr(row++, 1, "Event Information");

  put_simple_field("Event Name", _event_info.event_name);
  put_simple_field("Track Name", _event_info.track_name);
  put_simple_field("Session Type", _event_info.session_type);
  put_simple_field("Session Status", _event_info.session_status);
  put_simple_field("Track Time", _event_info.track_time);
  put_simple_field("Flag Status", _event_info.flag_status);
  put_simple_field(
      "Laps",
      std::format("{} / {}",
                  _event_info.completed_laps.has_value()
                      ? std::format("{}", _event_info.completed_laps.value())
                      : "--",
                  _event_info.total_laps.has_value()
                      ? std::format("{}", _event_info.total_laps.value())
                      : "--"));
}
}; // namespace Telemetry
