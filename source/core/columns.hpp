#pragma once

#include <cstdint>
#include <memory>

#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>
#include <type_traits>

#include "telemetry/driver_telemetry.hpp"
#include "tools/draw.hpp"
#include "ui/ui.hpp"

namespace Columns {

template <typename T>
concept ColumnKind =
    requires(T t) {
      // Must have COL_WIDTH, COL_NAME, and must define operator()(driver
      // const&)
      t.COL_WIDTH;
      t.COL_NAME;
    } &&
    (std::is_invocable_r_v<std::string, T,
                           Telemetry::DriverTelemetry const &> ||
     std::is_invocable_r_v<std::wstring, T,
                           Telemetry::DriverTelemetry const &> ||
     std::is_invocable_r_v<UI::String, T, Telemetry::DriverTelemetry const &>);

struct Rank {
  std::string operator()(Telemetry::DriverTelemetry const &d) {
    return std::format("{:>4}", d.get_rank());
  }

  static constexpr std::string_view COL_NAME = "Rank";
  static constexpr int COL_WIDTH = 4;
};

struct DriverName {
  // std::string operator()(Telemetry::DriverTelemetry const &d) {
  //   return std::format("{:>23}", d.get_name());
  // }

  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    return (d.in_pit
                ? UI::String("PIT ", UI::Color::RED_SOFT, UI::Style::ITALIC)
                : UI::String("    ")) +
           std::format("{:>19}", d.get_name());
  }

  static constexpr std::string_view COL_NAME = "Driver Name";
  static constexpr int COL_WIDTH = 23;
};

struct Speed {
  std::wstring operator()(Telemetry::DriverTelemetry const &d) {
    return std::format(L"{:>08.2f}", d._telemetry.has_vehiclespeed()
                                         ? d._telemetry.vehiclespeed()
                                         : 0.0);
  }

  static constexpr std::string_view COL_NAME = "Speed";
  static constexpr int COL_WIDTH = 8;
};

struct LastPit {
  std::wstring operator()(Telemetry::DriverTelemetry const &d) {
    return (d._completed_lap.has_lastpittedlap())
               ? std::format(L"{:>6}", d._completed_lap.lastpittedlap())
               : std::format(L"{:>6}", L"--");
  }

  static constexpr std::string_view COL_NAME = "Last Pit";
  static constexpr int COL_WIDTH = 8;
};

struct Throttle {
  std::wstring operator()(Telemetry::DriverTelemetry const &d) {
    return Tools::Draw::prog_bar(
        (d._telemetry.has_throttle()
             ? static_cast<double>(d._telemetry.throttle())
             : 0.0) /
            100,
        8);
  }

  static constexpr std::string_view COL_NAME = "Throttle";
  static constexpr int COL_WIDTH = 8;
};

struct Brake {
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    return UI::String(
        Tools::Draw::prog_bar(
            (d._telemetry.has_breakpercentage()
                 ? static_cast<double>(d._telemetry.breakpercentage())
                 : 0.0) /
                100,
            8),
        UI::Color::GRAY_HARD);
  }

  static constexpr std::string_view COL_NAME = "Brake";
  static constexpr int COL_WIDTH = 8;
};

}; // namespace Columns
