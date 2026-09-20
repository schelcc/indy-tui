#pragma once

#include <cstddef>

// #include "core/input.hpp"
#include "core/session.hpp"

#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_queue.hpp"

#include "ui/widget.hpp"

namespace UI::Views {

struct TrackSessionView {
  UI::Table table{};

  static constexpr size_t ROWS = 2;
  static constexpr size_t COLS = 3;

  void update_and_render(Telemetry::TrackSession const &,
                         std::shared_ptr<ncpp::Plane>);

  TrackSessionView();
};

struct SessionStatusView {
  UI::Table table{};

  static constexpr size_t ROWS = 4;
  static constexpr size_t COLS = 2;

  void update_and_render(Core::Session const &, std::shared_ptr<ncpp::Plane>);

  SessionStatusView();
};

}; // namespace UI::Views
