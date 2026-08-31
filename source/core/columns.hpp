#pragma once

#include <cstdint>
#include <memory>

#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>
#include <shared_mutex>
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
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    return UI::String(Tools::Draw::prog_bar(
                          (d._telemetry.has_throttle()
                               ? static_cast<double>(d._telemetry.throttle())
                               : 0.0) /
                              100,
                          8),
                      UI::Color::GRAY_HARD);
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

struct LastTimingLine {
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    std::shared_lock lock(d._last_line_crossing_mtx);
    return d._last_line_crossing.value_or("--");
  }

  static constexpr std::string_view COL_NAME = "Last Timeline";
  static constexpr int COL_WIDTH = 15;
};

struct Gap {
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    return UI::String(std::format("{:>9}", d._results.has_behindleader()
                                               ? d._results.behindleader()
                                               : "--"));
  }

  static constexpr std::string_view COL_NAME = "Gap";
  static constexpr int COL_WIDTH = 9;
};

struct LiveGap {
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    return UI::String(std::format("{:>9}", d.get_gap()));
  }

  static constexpr std::string_view COL_NAME = "LiveGap";
  static constexpr int COL_WIDTH = 9;
};

struct Interval {
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    return UI::String(std::format("{:>9}", d._results.has_gappreceding()
                                               ? d._results.gappreceding()
                                               : "--"));
  }

  static constexpr std::string_view COL_NAME = "Interval";
  static constexpr int COL_WIDTH = 9;
};

struct LiveInterval {
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
    return UI::String(std::format("{:>9}", d.get_interval()));
  }

  static constexpr std::string_view COL_NAME = "LiveInterval";
  static constexpr int COL_WIDTH = 12;
};

struct LapsSincePit {
  std::string operator()(Telemetry::DriverTelemetry const &d) {
    return d._results.has_sincepitlaps()
               ? std::format("{:>5}", d._results.sincepitlaps())
               : std::format("{:>5}", "--");
  }

  static constexpr std::string_view COL_NAME = "LSP";
  static constexpr int COL_WIDTH = 5;
};

struct TireType {
  UI::String operator()(Telemetry::DriverTelemetry const &d) {
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
  static constexpr std::string_view COL_NAME = "Tire";
  static constexpr int COL_WIDTH = 5;
};

struct P2P {
  std::string operator()(Telemetry::DriverTelemetry const &d) {
    return std::format(
        "{:>3}/{:>3}/{:>5}",
        d._telemetry.has_otremain() ? d._telemetry.otremain() : 0,
        d._telemetry.has_otevent() ? d._telemetry.otevent() : 0,
        d._telemetry.has_otstatus() ? static_cast<int>(d._telemetry.otstatus())
                                    : -1);
  }

  static constexpr std::string_view COL_NAME = "P2P";
  static constexpr int COL_WIDTH = 13;
};

struct LapDist {
  std::string operator()(Telemetry::DriverTelemetry const &d) {
    return std::format("{:>10.5f}", d._telemetry.has_lapdistance()
                                        ? d._telemetry.lapdistance()
                                        : -1.0);
  }

  static constexpr std::string_view COL_NAME = "Dist";
  static constexpr int COL_WIDTH = 16;
};

}; // namespace Columns
