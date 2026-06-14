#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <google/protobuf/descriptor.h>
#include <iostream>
#include <latch>
#include <limits>
#include <ncpp/NCKey.hh>
#include <ncpp/Root.hh>
#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>
#include <optional>
#include <print>

#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXUserAgent.h>
#include <ixwebsocket/IXWebSocket.h>
#include <mutex>
#include <pthread.h>
#include <ranges>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>

#include "ErpMessage.pb.h"
#include "app_context.hpp"
#include "core/cli.hpp"
#include "session.hpp"
#include "telemetry.hpp"
#include "telemetry/driver_telemetry.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_frame.hpp"
#include "time.hpp"
#include "tools/appsync_resolver.hpp"
#include "tools/base64.hpp"
#include "tools/logger.hpp"
#include "tools/nc_helpers.hpp"

#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <unordered_map>

using Tools::AppSyncSession;

using std::chrono::duration_cast;
using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;

std::string_view evtype_to_str(ncinput in) {
  switch (in.evtype) {
  case NCTYPE_UNKNOWN:
    return "UNK";
  case NCTYPE_PRESS:
    return "PRESS";
  case NCTYPE_REPEAT:
    return "REPEAT";
  case NCTYPE_RELEASE:
    return "RELEASE";
  default:
    return "!MISSING_STR!";
  }
}

struct BasicLeaderboardWorker {
  ncpp::NotCurses &nc;
  std::atomic_flag &running;
  Core::Session &sess;

  static constexpr Time::Duration::UIntMilliSec MAX_REDRAW_HZ =
      Time::Duration::UIntMilliSec(100);

  void operator()() {
    Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

    std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

    Time::Duration::DblMilliSec field_populate_duration;
    Time::TimePoint last_tick;

    Telemetry::TelemetryBoard board{};

    double render_hz = 0;

    last_tick = Time::Clock::now();

    int margin_left = 2;
    int margin_right = 0;
    int margin_top = 6;
    int margin_bottom = 2;

    std::shared_ptr<ncpp::Plane> board_plane = std::make_shared<ncpp::Plane>(
        std_plane.get(), std_plane->get_dim_y() - (margin_top + margin_bottom),
        std_plane->get_dim_x() - (margin_left + margin_right), margin_top,
        margin_left);

    if (!board.add_column(board_plane, Columns::Rank{}))
      return;
    if (!board.add_column(board_plane, Columns::DriverName{}))
      return;
    if (!board.add_column(board_plane, Columns::Speed{}))
      return;
    if (!board.add_column(board_plane, Columns::Throttle{}))
      return;

    // std::vector<std::shared_ptr<ncpp::Plane>> column_planes{};

    // const int header_offset = 6;

    // int col_x = 0;
    // int col_y = header_offset;

    // const int num_rows = 34;

    // // Helper to wrap adding columns
    // auto add_col = [&column_planes, &col_x, &col_y, num_rows](int const cols)
    // {
    //   column_planes.push_back(
    //       std::make_shared<ncpp::Plane>(num_rows, cols, col_y, col_x));
    //   col_x += cols;
    // };

    // // TODO: Add naming to columns

    // // Rank
    // add_col(4);
    // // Name
    // add_col(23);
    // // Speed
    // add_col(8);

    while (running.test()) {
      auto render_start = Time::Clock::now();
      auto block_until = render_start + MAX_REDRAW_HZ;

      int row = 1;

      // States printout
      std_plane->putstr(
          row++, 0,
          std::format("Field info population time: {:.2f} ms\t\t\t",
                      field_populate_duration.count())
              .c_str());

      std_plane->putstr(
          row++, 0,
          std::format("Frame rate: {:.2f} Hz\t\t\t", render_hz).c_str());

      std_plane->putstr(
          row++, 0,
          std::format("Accrued delay: {:.2f}s / {}s \t\t\t\t\t",
                      sess.get_accrued_delay_ms().count() / 1000.0,
                      sess.get_delay_sec().value_or(0))
              .c_str());

      // std_plane->putstr(row++, 0,
      //                   std::format("Accrued delay frames: {} frames",
      //                               sess.accrued_delay_frames())
      //                       .c_str());

      // std_plane->putstr(row++, 0,
      //                   std::format("Raw message recv. rate: {:.2f}
      //                   Hz\t\t\t\t",
      //                               sess.get_recv_hz())
      //                       .c_str());

      // std_plane->putstr(row++, 0,
      //                   std::format("Message enqueue rate: {:.2f}
      //                   Hz\t\t\t\t",
      //                               sess.get_enq_hz())
      //                       .c_str());
      // std_plane->putstr(row++, 0,
      //                   std::format("Log write rate: {:.2f} Hz\t\t\t\t",
      //                               Tools::Log::GetWriteRate())
      //                       .c_str());

      std_plane->putstr(row++, 0, "----------------------------");

      auto next_frame = sess.next_frame();
      if (next_frame.has_value()) {
        if (!board.inform_new_frame(std::move(next_frame.value()))
                 .has_value()) {
          Tools::Log::Warn("Unhandled board-render failure!", "MAIN-BOARD");
        }
      }
      // board.draw_basic(std_plane, row);
      board.draw_columns();
      row += board_plane->get_dim_y();

      std_plane->putstr(row++, 0, "----------------------------");

      nc.render();

      std_plane->erase();

      std::this_thread::sleep_until(block_until);

      Time::Duration::DblMilliSec gap = (Time::Clock::now() - render_start);

      render_hz = 1000.0 / gap.count();

      last_tick = Time::Clock::now();
    }

    Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
    nc.stop();
  }
};

struct KeyWorker {
  ncpp::NotCurses &nc;
  std::vector<ncpp::NCKey> &key_queue;
  std::atomic_flag &running;
  Core::Session &sess;
  size_t &delay;

  enum AllowedModifier {
    NONE,
    SHIFT,
    CTRL,
  };

  bool has_modifier(const ncinput *ni, const AllowedModifier mod) const {
    const bool has_ctrl = ncinput_ctrl_p(ni);
    const bool has_shift = ncinput_shift_p(ni);

    switch (mod) {
    case NONE:
      return (!has_shift && !has_ctrl);
    case SHIFT:
      return has_shift;
    case CTRL:
      return has_ctrl;
    default:
      return false;
    }
  }

  template <typename... Ts>
    requires(std::is_same_v<Ts, AllowedModifier> && ...)
  bool has_modifiers(const ncinput *ni, const Ts... mods) const {
    return (has_modifier(ni, mods) && ...);
  }

  bool key_char_is(const wchar_t key, const ncinput *ni) const {
    return ((*ni->utf8 == key) && has_modifier(ni, NONE));
  }

  template <typename... Ts>
    requires(std::is_same_v<Ts, AllowedModifier> && ...)
  bool key_char_is(const wchar_t key, const ncinput *ni,
                   const Ts... mods) const {
    return ((*ni->utf8 == key) && (has_modifier(ni, mods) && ...));
  }

  static constexpr struct timespec INPUT_TIMEOUT{.tv_sec = 5, .tv_nsec = 0};

  void operator()() {
    Tools::Log::Debug("Started key worker", "WORKER-INPUT");

    nc.linesigs_disable();

    while (running.test()) {
      ncinput in{};

      if (nc.get(&INPUT_TIMEOUT, &in) == 0)
        continue;

      if (in.evtype == ncpp::EvType::Release) {
        // Release-only events
        if (key_char_is('=', &in, SHIFT)) {
          Tools::Log::Debug("Increase delay requested", "WORKER-INPUT");
          sess.set_delay_sec(++delay);
        } else if (key_char_is('-', &in)) {
          Tools::Log::Debug("Decrease delay requested", "WORKER-INPUT");
          sess.set_delay_sec(delay == 0 ? 0 : --delay);
        } else if (key_char_is('q', &in) || (key_char_is('C', &in, CTRL) &&
                                             !key_char_is('C', &in, SHIFT))) {
          Tools::Log::Debug("Quit requrested", "WORKER-INPUT");
          App::AppContext::Shutdown("Quit requested by user");
        }
      }
    }
  }
};

int main([[maybe_unused]] const int argc, [[maybe_unused]] const char *argv[]) {
  Tools::Log::SetOut("indycpp.log");
  Tools::Log::SetLevel(Tools::Log::DEBUG);

  using namespace CLI;

  Parser parser =
      Parser()
          .set_tool_name("indy-tui")
          .set_desc("A cool little toy for following races from the terminal.");

  parser.add_element<Arg>("mode")
      .set_help_msg("What mode of operation to use. Options are 'live', "
                    "'live-debug', and "
                    "'replay'.")
      .set_handler([](std::string_view const s) -> Core::SessionSource {
        if (s == "live")
          return Core::SessionSource::SERVED_REMOTE;
        else if (s == "live-debug")
          return Core::SessionSource::SERVED_DEBUG;
        else if (s == "replay")
          return Core::SessionSource::LOCAL_REPLAY;
        else
          throw CLI::InputParsingErr(
              std::format("Invalid input for mode: '{}'", s));
      });

  parser.add_element<Option>("url")
      .set_help_msg("What websocket URL to reach for the 'live-debug' mode.")
      .set_long_opt("url")
      .set_short_opt("u");

  parser.add_element<Option>("log output")
      .set_help_msg("Where the log will be written to, if logging is enabled")
      .set_long_opt("log-out")
      .set_default_input("indy-tui.log")
      .set_handler([](std::string_view const s) { return s; });

  parser.add_element<Option>("log level")
      .set_help_msg(
          "What level of information to include in the logging, if "
          "enabled. Options are 'none', 'warn' 'info', 'debug', 'debug2'.")
      .set_long_opt("log-level")
      .set_default_input("info")
      .set_handler([](std::string_view const s) -> Tools::Log::Level {
        if (s == "none")
          return Tools::Log::NONE;
        else if (s == "warn")
          return Tools::Log::WARN;
        else if (s == "info")
          return Tools::Log::INFO;
        else if (s == "debug")
          return Tools::Log::DEBUG;
        else if (s == "debug2")
          return Tools::Log::DEBUG2;
        else
          throw CLI::InputParsingErr(
              std::format("Invalid input for log level: '{}'", s));
      });

  parser.finalize();

  try {
    parser.parse_input(argc, argv);
  } catch (CLI::InputParsingErr &e) {
    std::println("Error encountered parsing command line input: {}", e.what());
    std::println("{}", parser.get_help());
    return EXIT_FAILURE;
  }

  Tools::Log::SetOut(parser.get<Option, std::string_view>("log output"));
  Tools::Log::SetLevel(parser.get<Option, Tools::Log::Level>("log level"));

  std::vector<ncpp::NCKey> key_queue{};

  std::atomic_flag running{true};
  Core::Session sess(parser.get<Arg, Core::SessionSource>("mode"));

  size_t delay = 2;

  sess.set_delay_sec(delay);

  if (!sess.start_session())
    return EXIT_FAILURE;

  setlocale(LC_ALL, "");
  notcurses_options nc_opts{};

  // nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_NO_QUIT_SIGHANDLERS;
  nc_opts.flags = NCOPTION_NO_QUIT_SIGHANDLERS;

  ncpp::NotCurses nc{nc_opts};

  std::thread output_thread{BasicLeaderboardWorker{nc, running, sess}};
  std::thread input_thread{KeyWorker{nc, key_queue, running, sess, delay}};

  pthread_setname_np(output_thread.native_handle(), "Display");
  pthread_setname_np(input_thread.native_handle(), "Input");

  App::AppContext::AwaitShutdown();
  Tools::Log::Info(
      std::format("Shutdown requested for reason '{}'",
                  App::AppContext::GetReason().value_or(
                      "no reason provided -- ungraceful shutdown")),
      "MAIN");

  running.clear();

  input_thread.join();
  output_thread.join();

  sess.end_session();

  Tools::Log::Info("Exiting (graceful)...");
  std::println("Done.");
}
