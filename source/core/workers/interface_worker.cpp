#include <condition_variable>
#include <memory>
#include <ncpp/NCKey.hh>
#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>
#include <pthread.h>
#include <stop_token>

#include "core/app_context.hpp"
#include "core/columns.hpp"
#include "core/input.hpp"
#include "core/session.hpp"
#include "core/time.hpp"
#include "core/workers.hpp"

#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_queue.hpp"

#include "ui/layout.hpp"
#include "ui/types.hpp"
#include "ui/views.hpp"
#include "ui/widget.hpp"

using Telemetry::SessionType;

namespace Workers {

// Helper to construct the vector to pass into BlockList
std::vector<Layout::Block> make_row(auto &&...blocks) {
  std::vector<Layout::Block> output{};
  output.reserve(sizeof...(blocks));
  (output.emplace_back(std::move(blocks)), ...);
  return output;
}

void InterfaceWorker::operator()(std::stop_token stop_tok) {
  App::AppContext::Log().debug("Started leaderboard worker");

  // plane drawing calls take shared lock, nc.render() call takes unique
  struct TurnBasedMutex {
    std::shared_lock<std::shared_mutex> lock_shared() {
      permission_mtx.lock_shared();
      std::shared_lock lock(action_mtx);
      permission_mtx.unlock_shared();
      return lock;
    }

    std::unique_lock<std::shared_mutex> lock_unique() {
      permission_mtx.lock();
      std::unique_lock lock(action_mtx);
      permission_mtx.unlock();
      return lock;
    }

  private:
    std::shared_mutex permission_mtx;
    std::shared_mutex action_mtx;
  } render_mtx;

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
        [this](UI::RenderLevel render_level, std::shared_ptr<ncpp::Plane> p,
               bool focused,
               UI::Views::View &view) -> std::expected<void, UI::RenderErr> {
          auto &v = view.as<UI::Views::SessionStatusView>();

          return (render_level == UI::RenderLevel::FULL)
                     ? v.update_and_render(sess, focused, p)
                     : v.update_and_render(focused, p);
        });

    event_status_view =
        UI::Views::View::MakeView<UI::Views::TrackSessionView>();

    event_status_view->set_render_callback(
        [&board](UI::RenderLevel render_level, std::shared_ptr<ncpp::Plane> p,
                 bool focused,
                 UI::Views::View &view) -> std::expected<void, UI::RenderErr> {
          auto &v = view.as<UI::Views::TrackSessionView>();

          return (render_level == UI::RenderLevel::FULL)
                     ? v.update_and_render(board.session_info, focused, p)
                     : v.update_and_render(focused, p);
        });

    leaderboard_view = UI::Views::View::MakeView<UI::Views::LeaderboardView>();

    leaderboard_view->set_render_callback(
        [&board](UI::RenderLevel render_level, std::shared_ptr<ncpp::Plane> p,
                 bool focused,
                 UI::Views::View &view) -> std::expected<void, UI::RenderErr> {
          auto &v = view.as<UI::Views::LeaderboardView>();

          return (render_level == UI::RenderLevel::FULL)
                     ? v.update_and_render(board.reorder_and_get(), focused, p)
                     : v.update_and_render(focused, p);
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

  auto res = root_layout.solve(std_plane);
  if (!res.has_value()) {
    App::AppContext::Log().error("Error solving initial root layout: {}",
                                 res.error().what());

    App::AppContext::Shutdown("Failed to solve initial root layout: " +
                              res.error().what());
    return;
  }

  std::jthread render_thread =
      std::jthread{[&render_mtx, this](std::stop_token stop_tok) {
        while (!stop_tok.stop_requested()) {
          std::this_thread::sleep_for(Time::Duration::UIntMilliSec(50));
          std::unique_lock lock = render_mtx.lock_unique();
          nc.render();
        }
      }};

  pthread_setname_np(render_thread.native_handle(), "NC rendering");

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

    auto root_input_callback = [&root_layout,
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

  using Telemetry::TelemetryBoard;
  using Telemetry::TelemetryQueue;

  auto board_inform = [](Core::Session &sess, TelemetryBoard &board)
      -> std::expected<void,
                       std::variant<TelemetryBoard::Err, TelemetryQueue::Err>> {
    auto queue_res = sess.next_frame();

    if (!queue_res.has_value())
      return std::unexpected(queue_res.error());

    return board.inform_new_frame(std::move(queue_res.value()));
  };

  auto last_full_render = Time::Clock::now();
  bool init_render_done = false;

  std::expected<void, UI::RenderErr> render_res;
  std::expected<void, UI::RenderErr> solve_res;

  while (!stop_tok.stop_requested()) [[likely]] {

    std::this_thread::sleep_for(Time::Duration::UIntMilliSec(10));

    if ((nc.get(false, &in) != 0) && (in.evtype == ncpp::EvType::Press)) {
      std::lock_guard lock(key_queue_mtx);
      key_queue.emplace(Input::KeyWithMod(in));
      key_cond.notify_one();
    }

    UI::RenderLevel render_level =
        (!init_render_done || board_inform(sess, board).has_value() ||
         ((Time::Clock::now() - last_full_render) >
          Time::Duration::UIntMilliSec(100)))
            ? UI::RenderLevel::FULL
            : UI::RenderLevel::LIGHT;

    init_render_done =
        init_render_done || (render_level == UI::RenderLevel::FULL);

    last_full_render = (render_level == UI::RenderLevel::FULL)
                           ? Time::Clock::now()
                           : last_full_render;

    {
      auto lock = render_mtx.lock_shared();

      render_res = root_layout.render(render_level, true);

      if (!render_res.has_value()) {
        UI::RenderErr &e = render_res.error();

        if (e.reason == UI::RenderErr::NOP) {
          App::AppContext::Log().warn(
              "Allowing render NOP to fail passively: {}", e.what());
        } else {
          App::AppContext::Log().error("Failed to render: {}", e.what());
          App::AppContext::Shutdown("Encountered error rendering: " + e.what());
          break;
        }
      }
    }

    // Possible optimization: use built in notcurses resize_cb
    // functionality and avoid this global check
    // Update: Best way will be to use it on only the std plane, and have that
    // trigger a solve call. Will need some sort of global state, I think
    // putting it in AppContext is best.

    // Note: For some reason if this is pre-render it doesn't work?
    if ((std_plane->get_dim_y() != pre_dim_y) ||
        (std_plane->get_dim_x() != pre_dim_x)) [[unlikely]] {
      solve_res = root_layout.solve(std_plane);
      if (!solve_res.has_value()) {
        App::AppContext::Log().error("Failed to re-solve root layout: {}",
                                     res.error().what());
        App::AppContext::Shutdown("Encounted error re-solving root layout: " +
                                  res.error().what());
        break;
      }
    }

    std_plane->get_dim(&pre_dim_y, &pre_dim_x);

    last_tick = Time::Clock::now();
  }

  input_thread.request_stop();
  render_thread.request_stop();

  App::AppContext::Log().debug("Exiting leaderboard loop");
}

}; // namespace Workers
