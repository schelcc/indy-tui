#pragma once

#include <cstdint>
#include <memory>

#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>
#include <shared_mutex>
#include <type_traits>

#include "telemetry/driver_telemetry.hpp"
#include "telemetry_board.hpp"
#include "tools/draw.hpp"
#include "ui/string.hpp"
#include "ui/ui.hpp"

namespace Columns {

template <typename T>
concept ColumnKind =
    requires(T t) {
      // Must have COL_WIDTH, COL_NAME, and must define Speed(driver
      // const&)
      t.COL_WIDTH;
      t.COL_NAME;
    } &&
    (std::is_invocable_r_v<std::string, T,
                           Telemetry::DriverTelemetry const &> ||
     std::is_invocable_r_v<std::wstring, T,
                           Telemetry::DriverTelemetry const &> ||
     std::is_invocable_r_v<UI::String, T, Telemetry::DriverTelemetry const &>);

static inline std::string Rank(Telemetry::DriverTelemetry const &d) {
  return std::format("{}", d.get_rank());
}

template <Telemetry::SessionType PickSession = Telemetry::SessionType::RACE>
static inline UI::String PitStatus(Telemetry::DriverTelemetry const &d) {
  if constexpr (PickSession == Telemetry::SessionType::RACE)
    // In races, pit indicator should be red
    return d.in_pit ? UI::String("PIT", UI::Color::RED_SOFT, UI::Style::ITALIC)
                    : "   ";
  else
    // In practice and qualifying, pit indicator should be grey
    return d.in_pit ? UI::String("PIT", UI::Color::GRAY_HARD, UI::Style::ITALIC)
                    : "   ";
}

template <Telemetry::SessionType PickSession = Telemetry::SessionType::RACE>
static inline UI::String DriverName(Telemetry::DriverTelemetry const &d) {
  if constexpr (PickSession == Telemetry::SessionType::RACE)
    // Drivers' names should not change at all for races
    return d.get_name();
  else
    return d.in_pit ? UI::String(d.get_name(), UI::Color::GRAY_HARD,
                                 UI::Style::ITALIC)
                    : d.get_name();
}

static inline std::wstring Speed(Telemetry::DriverTelemetry const &d) {
  return std::format(L"{:>08.2f}", d._telemetry.has_vehiclespeed()
                                       ? d._telemetry.vehiclespeed()
                                       : 0.0);
}

static inline std::wstring LastPit(Telemetry::DriverTelemetry const &d) {
  return (d._completed_lap.has_lastpittedlap())
             ? std::format(L"{:>6}", d._completed_lap.lastpittedlap())
             : std::format(L"{:>6}", L"--");
}

static inline UI::String Throttle(Telemetry::DriverTelemetry const &d) {
  return UI::String(
      Tools::Draw::prog_bar((d._telemetry.has_throttle()
                                 ? static_cast<double>(d._telemetry.throttle())
                                 : 0.0) /
                                100,
                            8),
      UI::Color::GRAY_HARD);
}

static inline UI::String Brake(Telemetry::DriverTelemetry const &d) {
  return UI::String(
      Tools::Draw::prog_bar(
          (d._telemetry.has_breakpercentage()
               ? static_cast<double>(d._telemetry.breakpercentage())
               : 0.0) /
              100,
          8),
      UI::Color::GRAY_HARD);
}

static inline UI::String
LastTimingLine([[maybe_unused]] Telemetry::DriverTelemetry const &d) {
  return UI::String("--");
  // std::shared_lock lock(d._last_line_crossing_mtx);
  // return d._last_line_crossing.value_or("--");
}

static inline UI::String
Gap([[maybe_unused]] Telemetry::DriverTelemetry const &d) {
  if (d._completed_lap.has_lapsbehindleader() &&
      d._completed_lap.lapsbehindleader() > 0)
    return UI::String(
        std::format("{:>4} laps", -d._completed_lap.lapsbehindleader()));
  else
    return UI::String(std::format("{:>9.3f}", d.gap_to_leader.load()));
}

static inline UI::String Interval(Telemetry::DriverTelemetry const &d) {
  if (d._completed_lap.has_lapsbehindprec() &&
      d._completed_lap.lapsbehindprec() > 0)
    return UI::String(
        std::format("{:>4} laps", -d._completed_lap.lapsbehindprec()));
  else
    return UI::String(std::format("{:>9.3f}", d.interval_to_next.load()));
}

static inline std::string LapsSincePit(Telemetry::DriverTelemetry const &d) {
  return d._results.has_sincepitlaps()
             ? std::format("{:>5}", d._results.sincepitlaps())
             : std::format("{:>5}", "--");
}

static inline UI::String TireType(Telemetry::DriverTelemetry const &d) {
  if (!d._results.has_tiretype())
    return UI::String(std::format("{:>5}", "--"));

  auto const &tire = d._results.tiretype();
  if (tire == "P")
    return UI::String("P", UI::Color::BLACK_SOFT);
  else if (tire == "A")
    return UI::String("A", UI::Color::RED_SOFT);
  else
    return UI::String(tire);
}

static inline std::string P2P(Telemetry::DriverTelemetry const &d) {
  return std::format("{:>3}/{:>3}/{:>5}",
                     d._telemetry.has_otremain() ? d._telemetry.otremain() : 0,
                     d._telemetry.has_otevent() ? d._telemetry.otevent() : 0,
                     d._telemetry.has_otstatus()
                         ? static_cast<int>(d._telemetry.otstatus())
                         : -1);
}

static inline std::string LapDist(Telemetry::DriverTelemetry const &d) {
  return std::format("{:>10.5f}", d._telemetry.has_lapdistance()
                                      ? d._telemetry.lapdistance()
                                      : -1.0);
}

}; // namespace Columns
