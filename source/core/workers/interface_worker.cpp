#include <memory>

#include "columns.hpp"
#include "core/workers.hpp"

#include "core/time.hpp"
#include "draw.hpp"
#include "telemetry_board.hpp"
#include "tools/logger.hpp"
#include "ui/layout.hpp"
#include "views.hpp"
#include "widget.hpp"

using Telemetry::SessionType;

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

  auto header_plane = main_container.add_block(Segments(4));
  auto board_plane = main_container.add_block(Segments(8));
  auto footer_plane = main_container.add_block(Segments(1));

  // Build header layout
  Container<Direction::HORIZONTAL, Segments(3)> header_container(header_plane);

  auto header_info_plane = header_container.add_block(Segments(2));
  auto perf_info_plane = header_container.add_block(Segments(1));

  Container<Direction::VERTICAL, Segments(3)> header_info_container(
      header_info_plane);

  auto telem_status_plane = header_info_container.add_block(Segments(1));
  auto event_info_plane = header_info_container.add_block(Segments(2));

  UI::Views::SessionStatusView sess_status_view{};
  UI::Views::TrackSessionView event_status_view{};
  UI::Views::LeaderboardView leaderboard_view{};

  leaderboard_view.set_columns(
      {{Columns::Rank, "Rank"},
       {Columns::PitStatus<SessionType::PRACTICE>, " ", UI::Align::RIGHT},
       {Columns::DriverName<SessionType::PRACTICE>, "Name", UI::Align::RIGHT},
       {Columns::Speed, "Speed"},
       {Columns::Throttle, "Throttle"},
       {Columns::Brake, "Brake"},
       {Columns::LapDist, "Lap Dist."}});

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_HZ;

    auto next_frame = sess.next_frame();
    if (next_frame.has_value()) {
      if (!board.inform_new_frame(std::move(next_frame.value())).has_value()) {
        Tools::Log::Warn("Unhandled board-render failure!", "MAIN-BOARD");
      }
    }

    sess_status_view.update_and_render(sess, telem_status_plane);
    event_status_view.update_and_render(board.session_info, event_info_plane);
    leaderboard_view.update_and_render(board.reorder_and_get(), board_plane);

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
