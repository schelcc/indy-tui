#include <memory>

#include "columns.hpp"
#include "core/workers.hpp"

#include "core/time.hpp"
#include "draw.hpp"
#include "tools/logger.hpp"
#include "ui/layout.hpp"

namespace Workers {

void InterfaceWorker::operator()(std::stop_token stop_tok) {
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

  assert(board.add_column(board_plane, Columns::Rank{}));
  assert(board.add_column(board_plane, Columns::DriverName{}));
  assert(board.add_column(board_plane, Columns::Speed{}));
  assert(board.add_column(board_plane, Columns::Throttle{}));
  assert(board.add_column(board_plane, Columns::Brake{}));
  assert(board.add_column(board_plane, Columns::Gap{}));
  assert(board.add_column(board_plane, Columns::Interval{}));
  assert(board.add_column(board_plane, Columns::LapsSincePit{}));
  assert(board.add_column(board_plane, Columns::TireType{}));
  assert(board.add_column(board_plane, Columns::P2P{}));
  assert(board.add_column(board_plane, Columns::LapDist{}));

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_HZ;

    auto next_frame = sess.next_frame();
    if (next_frame.has_value()) {
      if (!board.inform_new_frame(std::move(next_frame.value())).has_value()) {
        Tools::Log::Warn("Unhandled board-render failure!", "MAIN-BOARD");
      }
    }

    Tools::Draw::border_with_title(board_plane, "Telemetry");

    board.draw_columns();
    board.draw_event_info(event_info_plane);
    sess.draw_telem_status(telem_status_plane);

    nc.render();

    std_plane->erase();

    std::this_thread::sleep_until(block_until);

    Time::Duration::DblMilliSec gap = (Time::Clock::now() - render_start);

    last_tick = Time::Clock::now();
  }

  Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
  nc.stop();
}

}; // namespace Workers
