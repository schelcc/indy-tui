#include <memory>

#include "core/workers.hpp"

#include "core/time.hpp"
#include "tools/logger.hpp"
#include "ui/layout.hpp"

namespace Workers {

void InterfaceWorker::operator()() {
  Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

  std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

  Time::TimePoint last_tick;

  Telemetry::TelemetryBoard board{};

  last_tick = Time::Clock::now();

  // Build main layout
  using namespace Layout;
  Container<Direction::VERTICAL, Segments(13)> main_container(std_plane);

  auto header_plane = main_container.add_block(Segments(3));
  auto board_plane = main_container.add_block(Segments(8));
  auto footer_plane = main_container.add_block(Segments(2));

  // Build header layout
  Container<Direction::HORIZONTAL, Segments(3)> header_container(header_plane);

  auto header_info_plane = header_container.add_block(Segments(2));
  auto perf_info_plane = header_container.add_block(Segments(1));

  Container<Direction::VERTICAL, Segments(3)> header_info_container(
      header_info_plane);

  auto telem_status_plane = header_info_container.add_block(Segments(1));
  auto event_info_plane = header_info_container.add_block(Segments(2));

  if (!board.add_column(board_plane, Columns::Rank{}))
    return;
  if (!board.add_column(board_plane, Columns::DriverName{}))
    return;
  if (!board.add_column(board_plane, Columns::Speed{}))
    return;
  if (!board.add_column(board_plane, Columns::Throttle{}))
    return;
  if (!board.add_column(board_plane, Columns::Brake{}))
    return;

  while (running.test()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_HZ;

    int row = 1;

    auto next_frame = sess.next_frame();
    if (next_frame.has_value()) {
      if (!board.inform_new_frame(std::move(next_frame.value())).has_value()) {
        Tools::Log::Warn("Unhandled board-render failure!", "MAIN-BOARD");
      }
    }

    board.draw_columns();
    board.draw_event_info(event_info_plane);
    sess.draw_telem_status(telem_status_plane);

    nc.render();

    std_plane->erase();

    std::this_thread::sleep_until(block_until);

    Time::Duration::DblMilliSec gap = (Time::Clock::now() - render_start);

    // render_hz = 1000.0 / gap.count();

    last_tick = Time::Clock::now();
  }

  Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
  nc.stop();
}

}; // namespace Workers
