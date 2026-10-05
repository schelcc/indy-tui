#include <condition_variable>
#include <exception>
#include <initializer_list>
#include <memory>
#include <ncpp/NCKey.hh>
#include <notcurses/nckeys.h>
#include <pthread.h>

#include "app_context.hpp"
#include "columns.hpp"
#include "core.hpp"
#include "core/workers.hpp"

#include "core/time.hpp"
#include "draw.hpp"
#include "input.hpp"
#include "telemetry_board.hpp"
#include "telemetry_frame.hpp"
#include "telemetry_queue.hpp"
#include "tools/logger.hpp"
#include "ui/layout.hpp"
#include "views.hpp"
#include "widget.hpp"

using Telemetry::SessionType;

namespace Workers {

void InterfaceWorker::operator()(std::stop_token stop_tok) {
  Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

  std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

  Telemetry::TelemetryBoard board{};

  // Create and configure views
  std::shared_ptr<UI::Views::View> sess_status_view =
      UI::Views::View::MakeView<UI::Views::SessionStatusView>();

  sess_status_view->set_render_callback([this](std::shared_ptr<ncpp::Plane> p,
                                               bool focused,
                                               UI::Views::View &view) -> void {
    view.as<UI::Views::SessionStatusView>().update_and_render(sess, focused, p);
  });

  std::shared_ptr<UI::Views::View> event_status_view =
      UI::Views::View::MakeView<UI::Views::TrackSessionView>();

  event_status_view->set_render_callback(
      [&board](std::shared_ptr<ncpp::Plane> p, bool focused,
               UI::Views::View &view) -> void {
        assert(p != nullptr);
        view.as<UI::Views::TrackSessionView>().update_and_render(
            board.session_info, focused, p);
      });

  std::shared_ptr<UI::Views::View> leaderboard_view =
      UI::Views::View::MakeView<UI::Views::LeaderboardView>();

  leaderboard_view->set_render_callback(
      [&board](std::shared_ptr<ncpp::Plane> p, bool focused,
               UI::Views::View &view) -> void {
        assert(p != nullptr);
        view.as<UI::Views::LeaderboardView>().update_and_render(
            board.reorder_and_get(), focused, p);
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
  // auto info_row =
  //     make_row(Block(1, event_status_view), Block(1, sess_status_view));

  auto info_row = BlockList(HORIZONTAL, make_row(Block(1, event_status_view),
                                                 Block(1, sess_status_view)));

  // BlockList root_layout(
  //     VERTICAL,
  //     make_row(
  //         Block(1, BlockList(HORIZONTAL, make_row(Block(1,
  //         event_status_view),
  //                                                 Block(1,
  //                                                 sess_status_view)))),
  //         Block(1, leaderboard_view)));

  BlockList root_layout(VERTICAL, make_row(Block(1, std::move(info_row)),
                                           Block(1, leaderboard_view)));

  root_layout.solve(std_plane);

  leaderboard_view->as<UI::Views::LeaderboardView>().set_columns(
      {{Columns::Rank, "Rank"},
       {Columns::PitStatus<SessionType::PRACTICE>, " ", UI::Align::RIGHT},
       {Columns::DriverName<SessionType::PRACTICE>, "Name", UI::Align::LEFT}});

  uint pre_dim_x = 0;
  uint pre_dim_y = 0;

  std_plane->get_dim(&pre_dim_y, &pre_dim_x);

  // Move this somewhere else later
  using Input::KeyWithMod;
  using Input::Modifier;
  Input::InputHandler root_handler{};
  root_handler
      .register_callback(
          KeyWithMod('=', Modifier::SHIFT), "Increase the delay by 1s",
          [this] { sess.set_delay_sec(sess.get_delay_sec().value_or(0) + 1); })
      .register_callback(KeyWithMod('-', Modifier::NONE),
                         "Decrease the delay by 1s only if delay is nonzero",
                         [this] {
                           int delay = sess.get_delay_sec().value_or(0);
                           sess.set_delay_sec((delay > 0) ? delay - 1 : 0);
                         })
      .register_callback(
          KeyWithMod('q', Modifier::NONE), "Exit the application",
          [] { App::AppContext::Shutdown("Shutdown requested by user"); })
      .duplicate_callback(KeyWithMod('q', Modifier::NONE),
                          KeyWithMod('C', Modifier::CTRL));

  auto root_input_callback = [&root_layout, handler = std::move(root_handler)](
                                 Input::KeyWithMod const &k) mutable {
    // Primary input callback. Invoked in other thread, so must be
    // mindful of thread safety. Should first handle global input,
    // then pass to root_layout for contextual input.
    if (handler.has_registered(k))
      handler.handle_input(k);
    else
      root_layout.take_input(k);
  };

  // Interface worker spawns and manages input thread
  std::jthread input_thread{
      Workers::KeyWorker{nc, key_cond, key_queue_mtx, key_queue, running, sess,
                         [f = std::move(root_input_callback)](
                             Input::KeyWithMod const &k) mutable { f(k); }}};

  pthread_setname_np(input_thread.native_handle(), "Input");

  ncinput in{};
  // bool skip_delay = false;

  Time::TimePoint last_tick = Time::Clock::now();

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_PERIOD;

    sess.next_frame()
        .transform(
            [&board](std::unique_ptr<Telemetry::TelemetryFrame> &&frame) {
              return board.inform_new_frame(std::move(frame));
            })
        .transform_error([](Telemetry::TelemetryQueue::Err const &e) {
          using Kind = Telemetry::TelemetryQueue::Err::Kind;
          if ((e.kind != Kind::DELAY_FULL) && (e.kind != Kind::TOO_RECENT))
            Tools::Log::Warn("Unhandled board-render failure", "MAIN-BOARD");
          return e;
        });

    root_layout.render(true);

    nc.render();

    std_plane->erase();

    if ((nc.get(false, &in) != 0) && (in.evtype == ncpp::EvType::Press)) {
      // skip_delay = true;
      std::lock_guard lock(key_queue_mtx);
      key_queue.emplace(Input::KeyWithMod(in));
      key_cond.notify_one();
    }

    // Possible optimization: use built in notcurses resize_cb functionality and
    // avoid this global check
    if ((std_plane->get_dim_y() != pre_dim_y) ||
        (std_plane->get_dim_x() != pre_dim_x)) {
      root_layout.solve(std_plane);
      // skip_delay = true;
    }
    // else
    //   std::this_thread::sleep_until(block_until);

    // if (!skip_delay)
    std::this_thread::sleep_until(block_until);

    // skip_delay = false;

    std_plane->get_dim(&pre_dim_y, &pre_dim_x);

    // Time::Duration::DblMilliSec gap = (Time::Clock::now() - render_start);

    last_tick = Time::Clock::now();
  }

  input_thread.request_stop();

  Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
}

}; // namespace Workers
