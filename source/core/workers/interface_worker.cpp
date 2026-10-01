#include <exception>
#include <initializer_list>
#include <memory>
#include <ncpp/NCKey.hh>
#include <notcurses/nckeys.h>

#include "columns.hpp"
#include "core.hpp"
#include "core/workers.hpp"

#include "core/time.hpp"
#include "draw.hpp"
#include "input.hpp"
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

  // Create and configure views
  std::shared_ptr<UI::Views::View> sess_status_view =
      UI::Views::View::MakeView<UI::Views::SessionStatusView>();

  sess_status_view->set_render_callback(
      [this](std::shared_ptr<ncpp::Plane> p, UI::Views::View &view) -> void {
        view.as<UI::Views::SessionStatusView>().update_and_render(sess, p);
      });

  std::shared_ptr<UI::Views::View> event_status_view =
      UI::Views::View::MakeView<UI::Views::TrackSessionView>();

  event_status_view->set_render_callback(
      [&board](std::shared_ptr<ncpp::Plane> p, UI::Views::View &view) -> void {
        assert(p != nullptr);
        view.as<UI::Views::TrackSessionView>().update_and_render(
            board.session_info, p);
      });

  std::shared_ptr<UI::Views::View> leaderboard_view =
      UI::Views::View::MakeView<UI::Views::LeaderboardView>();

  leaderboard_view->set_render_callback(
      [&board](std::shared_ptr<ncpp::Plane> p, UI::Views::View &view) -> void {
        assert(p != nullptr);
        view.as<UI::Views::LeaderboardView>().update_and_render(
            board.reorder_and_get(), p);
      });

  // Build main layout
  using namespace Layout;

  // Helper to construct the vector to pass into BlockList
  auto make_row = [](auto &&...blocks) -> std::vector<Block> {
    std::vector<Block> output{};
    output.reserve(sizeof...(blocks));
    (output.emplace_back(std::move(blocks)), ...);
    return output;
  };

  // Build rows
  auto info_row =
      make_row(Block(1, event_status_view), Block(1, sess_status_view));

  BlockList root_layout(
      VERTICAL, make_row(Block(1, BlockList(HORIZONTAL, std::move(info_row))),
                         Block(1, leaderboard_view)));

  root_layout.solve(std_plane);

  leaderboard_view->as<UI::Views::LeaderboardView>().set_columns(
      {{Columns::Rank, "Rank"},
       {Columns::PitStatus<SessionType::PRACTICE>, " ", UI::Align::RIGHT},
       {Columns::DriverName<SessionType::PRACTICE>, "Name", UI::Align::LEFT}});

  uint pre_dim_x = 0;
  uint pre_dim_y = 0;
  std_plane->get_dim(&pre_dim_y, &pre_dim_x);

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_HZ;

    auto next_frame = sess.next_frame();
    if (next_frame.has_value()) {
      if (!board.inform_new_frame(std::move(next_frame.value())).has_value()) {
        Tools::Log::Warn("Unhandled board-render failure!", "MAIN-BOARD");
      }
    }

    root_layout.render();

    nc.render();

    std_plane->erase();

    ncinput in{};
    if ((nc.get(false, &in) != 0) && (in.evtype == ncpp::EvType::Release)) {
      std::lock_guard lock(key_queue_mtx);
      key_queue.emplace(Input::KeyWithMod(in));
      key_cond.notify_one();
    }

    if ((std_plane->get_dim_y() != pre_dim_y) ||
        (std_plane->get_dim_x() != pre_dim_x))
      root_layout.solve(std_plane);
    else
      std::this_thread::sleep_until(block_until);

    std_plane->get_dim(&pre_dim_y, &pre_dim_x);

    Time::Duration::DblMilliSec gap = (Time::Clock::now() - render_start);

    last_tick = Time::Clock::now();
  }

  Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
}

}; // namespace Workers
