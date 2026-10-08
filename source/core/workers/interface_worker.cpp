#include <condition_variable>
#include <exception>
#include <initializer_list>
#include <memory>
#include <ncpp/NCKey.hh>
#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>
#include <pthread.h>

#include "app_context.hpp"
#include "columns.hpp"
#include "core.hpp"
#include "core/workers.hpp"

#include "core/time.hpp"
#include "draw.hpp"
#include "input.hpp"
#include "perf.hpp"
#include "telemetry_board.hpp"
#include "telemetry_frame.hpp"
#include "telemetry_queue.hpp"
#include "tools/logger.hpp"
#include "ui/layout.hpp"
#include "views.hpp"
#include "widget.hpp"

using Telemetry::SessionType;

namespace Workers {

// int base_resizecb(struct ncplane *p) {
//   return 0;
// }

// Helper to construct the vector to pass into BlockList
std::vector<Layout::Block> make_row(auto &&...blocks) {
  std::vector<Layout::Block> output{};
  output.reserve(sizeof...(blocks));
  (output.emplace_back(std::move(blocks)), ...);
  return output;
}

void InterfaceWorker::operator()(std::stop_token stop_tok) {
  Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

  std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

  Telemetry::TelemetryBoard board{};

  std::shared_ptr<UI::Views::View> sess_status_view;
  std::shared_ptr<UI::Views::View> event_status_view;
  std::shared_ptr<UI::Views::View> leaderboard_view;

  // Create and configure views
  {
    sess_status_view =
        UI::Views::View::MakeView<UI::Views::SessionStatusView>();

    sess_status_view->set_render_callback(
        [this](std::shared_ptr<ncpp::Plane> p, bool focused,
               UI::Views::View &view) -> void {
          view.as<UI::Views::SessionStatusView>().update_and_render(sess,
                                                                    focused, p);
        });

    event_status_view =
        UI::Views::View::MakeView<UI::Views::TrackSessionView>();

    event_status_view->set_render_callback(
        [&board](std::shared_ptr<ncpp::Plane> p, bool focused,
                 UI::Views::View &view) -> void {
          assert(p != nullptr);
          view.as<UI::Views::TrackSessionView>().update_and_render(
              board.session_info, focused, p);
        });

    leaderboard_view = UI::Views::View::MakeView<UI::Views::LeaderboardView>();

    leaderboard_view->set_render_callback(
        [&board](std::shared_ptr<ncpp::Plane> p, bool focused,
                 UI::Views::View &view) -> void {
          assert(p != nullptr);
          view.as<UI::Views::LeaderboardView>().update_and_render(
              board.reorder_and_get(), focused, p);
        });

    leaderboard_view->as<UI::Views::LeaderboardView>().set_columns(
        {{Columns::Rank, "Rank"},
         {Columns::PitStatus<SessionType::PRACTICE>, " ", UI::Align::RIGHT},
         {Columns::DriverName<SessionType::PRACTICE>, "Name", UI::Align::LEFT},
         {Columns::Interval, "Interval"},
         {Columns::Gap, "Gap"},
         {Columns::Throttle, "Throttle %"},
         {Columns::Brake, "Brake %"},
         {Columns::LapsSincePit, "Since Pit"},
         {Columns::Speed, "Speed"},
         {Columns::LapDist, "Lap Distance"}});
  }

  // Build main layout
  using namespace Layout;

  auto info_row = BlockList(HORIZONTAL, make_row(Block(2, event_status_view),
                                                 Block(1, sess_status_view)));

  BlockList root_layout(VERTICAL, make_row(Block(1, std::move(info_row)),
                                           Block(4, leaderboard_view)));

  root_layout.solve(std_plane);

  // Used for allowing early renders triggered from input thread
  std::mutex render_cv_mtx;
  std::condition_variable_any render_cv;

  // Configure root input handler, create and configure input thread;
  Input::InputHandler root_handler{};
  std::jthread input_thread;
  {
    using Input::KeyWithMod;
    using Input::Modifier;
    root_handler
        .register_callback(
            KeyWithMod('=', Modifier::SHIFT), "Increase the delay by 1s",
            [this] {
              sess.set_delay_sec(sess.get_delay_sec().value_or(0) + 1);
            })
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

    auto root_input_callback = [&root_layout, &render_cv, &render_cv_mtx,
                                handler = std::move(root_handler)](
                                   Input::KeyWithMod const &k) mutable {
      // Primary input callback. Invoked in other thread, so must be
      // mindful of thread safety. Should first handle global input,
      // then pass to root_layout for contextual input.
      bool handled = handler.has_registered(k);

      if (handled)
        handler.handle_input(k);
      else
        handled = root_layout.take_input(k);

      if (handled) {
        Tools::Log::Debug("Triggering early render", "ROOT-INPUT");
        std::lock_guard lock(render_cv_mtx);
        render_cv.notify_all();
      }
    };

    // Interface worker spawns and manages input thread
    input_thread = std::jthread{Workers::KeyWorker{
        nc, key_cond, key_queue_mtx, key_queue, running, sess,
        [f = std::move(root_input_callback)](
            Input::KeyWithMod const &k) mutable { f(k); }}};

    pthread_setname_np(input_thread.native_handle(), "Input");
  }

  ncinput in{};

  uint pre_dim_x = 0;
  uint pre_dim_y = 0;

  std_plane->get_dim(&pre_dim_y, &pre_dim_x);

  Time::TimePoint last_tick = Time::Clock::now();

  size_t cnt = 0;

  App::PerfContext::Get();

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_PERIOD;

    if ((cnt++ % 10) == 0) {
      App::PerfContext::LogKeyHandleTime();
      App::PerfContext::LogLeaderboardPopulationTime();
      App::PerfContext::LogRenderTime();
    }

    if (!sess.next_frame()
             // If next frame retrieved, hand it to the board
             .transform(
                 [&board](std::unique_ptr<Telemetry::TelemetryFrame> &&frame) {
                   return board.inform_new_frame(std::move(frame));
                 })
             // If the board handed back an error, let it pass on as an
             // error only if it is not one of the specified set of errors
             .transform_error(
                 [](Telemetry::TelemetryQueue::Err const &e)
                     -> std::expected<void, Telemetry::TelemetryQueue::Err> {
                   using Kind = Telemetry::TelemetryQueue::Err;
                   if ((e.kind == Kind::DELAY_FULL) ||
                       (e.kind == Kind::TOO_RECENT))
                     return {};
                   else
                     return e;
                 })
             .has_value())
      Tools::Log::Warn("Unhandled board-render failure", "MAIN-BOARD");

    if ((nc.get(false, &in) != 0) && (in.evtype == ncpp::EvType::Press)) {
      std::lock_guard lock(key_queue_mtx);
      key_queue.emplace(Input::KeyWithMod(in));
      key_cond.notify_one();
    }

    std::unique_lock lock(render_cv_mtx);
    render_cv.wait_until(lock, block_until);

    App::PerfContext::StartRenderTime();
    std_plane->erase();

    root_layout.render(true);

    nc.render();
    App::PerfContext::StopRenderTime();

    // Possible optimization: use built in notcurses resize_cb
    // functionality and avoid this global check
    // Update: Best way will be to use it on only the std plane, and have that
    // trigger a solve call. Will need some sort of global state, I think
    // putting it in AppContext is best.

    // Note: For some reason if this is pre-render it doesn't work?
    if ((std_plane->get_dim_y() != pre_dim_y) ||
        (std_plane->get_dim_x() != pre_dim_x)) {
      root_layout.solve(std_plane);
    }

    std_plane->get_dim(&pre_dim_y, &pre_dim_x);

    last_tick = Time::Clock::now();
  }

  input_thread.request_stop();

  Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
}

}; // namespace Workers
