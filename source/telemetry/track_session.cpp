#include "ErpMessage.pb.h"

#include "core/units.hpp"
#include "telemetry/driver_telemetry.hpp"
#include "telemetry/telemetry_board.hpp"

#include <cmath>

namespace Telemetry {

void TrackSession::update(proto::telemetry::ErpMessage const &msg) {
  if (msg.trackinformation_size() > 0) {
    auto const &info = msg.trackinformation().Get(0);

    if (!track_name.get_const()->has_value() && info.has_trackname())
      track_name.get_mut()->emplace(info.trackname());

    if (!lap_length.get_const()->has_value() && info.has_tracklength())
      lap_length.get_mut()->emplace(info.tracklength() *
                                    Units::METERS_PER_MILE);

    if (!num_checkpts.get_const()->has_value() && info.has_tracklength())
      num_checkpts.get_mut()->emplace(static_cast<size_t>(
          std::ceil((info.tracklength() * Units::METERS_PER_MILE) /
                    DriverTelemetry::CHECKPOINT_DIST)));

    if (!track_type.get_const()->has_value() && info.has_tracktype())
      track_type.get_mut()->emplace(info.tracktype());
  }

  if (msg.heartbeats_size() > 0) {
    auto const &hbeat = msg.heartbeats().Get(0);

    // One-time fields

    if (!event_name.get_const()->has_value() && hbeat.has_eventname())
      event_name.get_mut()->emplace(hbeat.eventname());

    if (!series_name.get_const()->has_value() && hbeat.has_series())
      series_name.get_mut()->emplace(hbeat.series());

    // Upon-change fields

    if (hbeat.has_currentflag() &&
        (!flag_status.get_const()->has_value() ||
         flag_status.get_const()->value().text != hbeat.currentflag())) {
      std::optional<FlagStatus> new_flag_status{std::nullopt};

      auto const &cur_flag = hbeat.currentflag();

      if (cur_flag == "GREEN")
        new_flag_status = FlagStatus::GREEN;
      else if (cur_flag == "YELLOW")
        new_flag_status = FlagStatus::YELLOW;
      else if (cur_flag == "RED")
        new_flag_status = FlagStatus::RED;
      else if (cur_flag == "CHECKERED")
        new_flag_status = FlagStatus::CHECKERED;

      if (new_flag_status.has_value())
        flag_status.get_mut()->emplace(hbeat.currentflag(),
                                       new_flag_status.value());
      else
        flag_status.get_mut()->reset();
    }

    if (!session_type.get_const()->has_value() && hbeat.has_sessiontype()) {
      // TODO: Figure out mapping sessiontype str to enum
      session_type.get_mut()->emplace(SessionType::PRACTICE);
    }

    if (hbeat.has_sessionstatus() &&
        (hbeat.sessionstatus() != session_status.get_const()->value_or("")))
      session_status.get_mut()->emplace(hbeat.sessionstatus());

    if (hbeat.has_overalltimetogo() &&
        (hbeat.overalltimetogo() != time_to_go.get_const()->value_or("")))
      time_to_go.get_mut()->emplace(hbeat.overalltimetogo());

    if (hbeat.has_completedlaps() &&
        (hbeat.completedlaps() != completed_laps.get_const()->value_or(0)))
      completed_laps.get_mut()->emplace(hbeat.completedlaps());

    if (hbeat.has_totallaps() &&
        (hbeat.totallaps() != total_laps.get_const()->value_or(0)))
      total_laps.get_mut()->emplace(hbeat.totallaps());
  }
}

}; // namespace Telemetry
