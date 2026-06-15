#pragma once

#include <cstdint>
#include <memory>

#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>
#include <type_traits>

#include "telemetry/driver_telemetry.hpp"
#include "tools/draw.hpp"

namespace Columns {

template <typename T>
concept ColumnKind =
    requires(T t) {
      // Must have COL_WIDTH, COL_NAME, and must define operator()(row, driver
      // telem, plane)
      t.COL_WIDTH;
      t.COL_NAME;
    } &&
    std::is_invocable_v<T, size_t const, Telemetry::DriverTelemetry const &,
                        std::shared_ptr<ncpp::Plane>>;

struct Rank {
  void operator()(size_t const row, Telemetry::DriverTelemetry const &d,
                  std::shared_ptr<ncpp::Plane> p) {
    p->putstr(row, 1, std::format("{:>4}", d.get_rank()).data());
  }

  static constexpr std::string_view COL_NAME = "Rank";
  static constexpr int COL_WIDTH = 4;
};

struct DriverName {
  void operator()(size_t const row, Telemetry::DriverTelemetry const &d,
                  std::shared_ptr<ncpp::Plane> p) {
    p->putstr(row, 1, std::format("{:>23}", d.get_name()).data());
  }

  static constexpr std::string_view COL_NAME = "Driver Name";
  static constexpr int COL_WIDTH = 23;
};

struct Speed {
  void operator()(size_t const row, Telemetry::DriverTelemetry const &d,
                  std::shared_ptr<ncpp::Plane> p) {
    p->putstr(row, 1, std::format("{:>08.2f}", d.get_speed()).data());
  }

  static constexpr std::string_view COL_NAME = "Speed";
  static constexpr int COL_WIDTH = 8;
};

struct LastPit {
  void operator()(size_t const row, Telemetry::DriverTelemetry const &d,
                  std::shared_ptr<ncpp::Plane> p) {
    if (d._completed_lap.has_lastpittedlap())
      p->putstr(row, 1,
                std::format("{:>6}", d._completed_lap.lastpittedlap()).data());
    else
      p->putstr(row, 1, std::format("{:>6}", "--").data());
  }

  static constexpr std::string_view COL_NAME = "Last Pit";
  static constexpr int COL_WIDTH = 8;
};

struct Throttle {
  void operator()(size_t const row, Telemetry::DriverTelemetry const &d,
                  std::shared_ptr<ncpp::Plane> p) {
    auto progbar = Tools::Draw::prog_bar(
        (d._telemetry.has_throttle()
             ? static_cast<double>(d._telemetry.throttle())
             : 0.0) /
            100,
        8);
    p->putstr(row, 1, progbar.c_str());
  }

  static constexpr std::string_view COL_NAME = "Throttle";
  static constexpr int COL_WIDTH = 8;
};

struct Brake {
  void operator()(size_t const row, Telemetry::DriverTelemetry const &d,
                  std::shared_ptr<ncpp::Plane> p) {
    auto progbar = Tools::Draw::prog_bar(
        (d._telemetry.has_breakpercentage()
             ? static_cast<double>(d._telemetry.breakpercentage())
             : 0.0) /
            100,
        8);
    p->putstr(row, 1, progbar.c_str());
  }

  static constexpr std::string_view COL_NAME = "Brake";
  static constexpr int COL_WIDTH = 8;
};

}; // namespace Columns
