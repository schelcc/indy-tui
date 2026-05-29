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
#include <ranges>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>

#include "ErpMessage.pb.h"
#include "session.hpp"
#include "telemetry.hpp"
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

  void operator()() {
    Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

    std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

    Time::Duration::DblMilliSec field_populate_duration;
    Time::TimePoint last_tick;

    Telemetry::TelemetryBoard board{};

    double render_hz = 0;

    last_tick = Time::Clock::now();

    while (running.test()) {
      auto render_start = Time::Clock::now();

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
          std::format("Accrued delay: {:.2f}s\t\t\t\t\t",
                      sess.get_accrued_delay_ms().count() / 1000.0)
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
        } else {
          board.draw_basic(std_plane, row);
        }
      }

      std_plane->putstr(row++, 0, "----------------------------");

      nc.render();

      std_plane->erase();

      Time::Duration::DblMilliSec gap = (Time::Clock::now() - render_start);

      render_hz = 1000.0 / gap.count();

      last_tick = Time::Clock::now();
    }

    Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
    nc.stop();
  }
};

struct TimeOfDayWorker {
  Telemetry::TelemetrySession &sess;
  std::atomic_bool &running;

  void operator()() {
    proto::telemetry::ErpMessage telem_frame;
    std::string telem_dbg;
    while (running.load()) {
      auto block_time = sess.get_block_time();

      if (sess.ripe()) {
        auto res = sess.swap_telemetry(std::move(telem_frame));

        if (telem_frame.heartbeats_size() > 0) {
          auto &heartbeat = telem_frame.heartbeats().Get(0);

          if (heartbeat.has_timeofday()) {
            Tools::Log::Debug(
                std::format("\t\tTime of day: {}", heartbeat.timeofday()),
                "OUTPUT");
          }
        }
      }

      std::this_thread::sleep_until(block_time);
    }
  }
};

struct TimeLeftWorker {
  Telemetry::TelemetrySession &sess;
  std::atomic_bool &running;

  void operator()() {
    proto::telemetry::ErpMessage telem_frame;
    std::string telem_dbg;
    while (running.load()) {
      auto block_time = sess.get_block_time();

      if (sess.ripe()) {
        auto res = sess.swap_telemetry(std::move(telem_frame));

        if (telem_frame.heartbeats_size() > 0) {
          auto heartbeat = telem_frame.heartbeats().Get(0);

          if (heartbeat.has_overalltimetogo()) {

            std::div_t min_div =
                std::div(std::stoi(heartbeat.overalltimetogo()), 60);
            std::div_t hr_div = std::div(min_div.quot, 60);

            Tools::Log::Debug(std::format("\t\tTime to go: {}:{}:{}",
                                          hr_div.quot, hr_div.rem, min_div.rem),
                              "OUTPUT");
          }
        }
      }

      std::this_thread::sleep_until(block_time);
    }
  }
};

struct CrudeSpeedboardWorker {
  Telemetry::TelemetrySession &sess;
  std::atomic_bool &running;

  void operator()() {
    proto::telemetry::ErpMessage telem_frame;
    while (running.load()) {
      auto block_time = sess.get_block_time();

      if (sess.ripe()) {
        auto res = sess.swap_telemetry(std::move(telem_frame));
        printf("\033[H\033[J");

        for (auto const &row : telem_frame.telemetrymessages()) {
          std::println("#{}   SPEED[{} MPH]  ENGINE[{} RPM]  GEAR[{}]",
                       row.carnumber(), row.vehiclespeed(), row.enginespeed(),
                       row.gear());
        }
        if (telem_frame.heartbeats_size() > 0) {
          auto heartbeat = telem_frame.heartbeats().Get(0);

          if (heartbeat.has_overalltimetogo()) {

            std::div_t min_div =
                std::div(std::stoi(heartbeat.overalltimetogo()), 60);
            std::div_t hr_div = std::div(min_div.quot, 60);

            std::println("\t\tTime to go: {}:{}:{}", hr_div.quot, hr_div.rem,
                         min_div.rem);
          }
        }
      }

      std::this_thread::sleep_until(block_time);
    }
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

  static constexpr struct timespec INPUT_TIMEOUT{.tv_sec = 1, .tv_nsec = 0};

  void operator()() {
    Tools::Log::Debug("Started key worker", "WORKER-INPUT");

    nc.linesigs_disable();

    while (running.test()) {
      ncinput in{};

      if (nc.get(&INPUT_TIMEOUT, &in) == 0)
        continue;

      // clang-format off
      // Tools::Log::Debug(std::format("UTF8: {}", in.utf8), "KEY-PRESSED");
      // Tools::Log::Debug(std::format("BUILTIN SHIFT: {}", in.shift), "KEY-PRESSED");
      // Tools::Log::Debug(std::format("BUILTIN CTRL: {}", in.ctrl), "KEY-PRESSED");
      // Tools::Log::Debug(std::format("BUILTIN ALT: {}", in.alt), "KEY-PRESSED");
      // Tools::Log::Debug(std::format("EFF. TEXT: {}", in.eff_text), "KEY-PRESSED");
      // Tools::Log::Debug(std::format("PRED. CTRL: {}", ncinput_ctrl_p(&in)), "KEY-PRESSED");
      // Tools::Log::Debug(std::format("PRED. SHIFT: {}", ncinput_shift_p(&in)), "KEY-PRESSED");
      // Tools::Log::Debug(std::format("PRED. ALT: {}", ncinput_alt_p(&in)), "KEY-PRESSED");
      // clang-format on

      if (in.evtype == ncpp::EvType::Release) {
        // Release-only events
        if (key_char_is('=', &in, SHIFT)) {
          Tools::Log::Debug("Increase delay requested", "WORKER-INPUT");
          sess.set_delay_sec(++delay);
        } else if (key_char_is('-', &in)) {
          Tools::Log::Debug("Decrease delay requested", "WORKER-INPUT");
          sess.set_delay_sec(delay == 1 ? 1 : --delay);
        } else if (key_char_is('q', &in) || (key_char_is('C', &in, CTRL) &&
                                             !key_char_is('C', &in, SHIFT))) {
          Tools::Log::Debug("Quit requrested", "WORKER-INPUT");
          kill(0, SIGINT);
        }
      }
    }
  }
};

// void sigint_handler(int s) {

// }

int main(int argc, char *argv[]) {
  Tools::Log::SetOut("indycpp.log");
  Tools::Log::SetLevel(Tools::Log::DEBUG);

  // https://thomastrapp.com/blog/signal-handlers-for-multithreaded-cpp/
  sigset_t sigset;

  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);
  sigaddset(&sigset, SIGUSR1);

  // Block signals in sigset from being handled by this and all children threads
  pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

  // std::atomic_bool shutdown_requested{false};
  // std::mutex sig_cv_mtx;
  // std::condition_variable sig_cv;

  // std::latch terminate_latch{2};

  // auto sig_shutdown_handler = [&terminate_latch, &sigset] {
  //   int sig_num = 0;

  //   // Wait for a signal in sigset
  //   sigwait(&sigset, &sig_num);
  //   terminate_latch.arrive_and_wait();
  //   Tools::Log::Debug(std::format("Got signal {}", strsignal(sig_num)),
  //                     "SIG-HANDLER");
  //   return;
  // };

  // std::thread sig_thread = std::thread(sig_shutdown_handler);

  // auto main_worker = [&terminate_latch] {
  std::vector<ncpp::NCKey> key_queue{};

  std::atomic_flag running{true};
  Core::Session sess(Core::SessionSource::SERVED_DEBUG);

  size_t delay = 10;

  sess.set_delay_sec(delay);

  if (!sess.start_session())
    return EXIT_FAILURE;

  setlocale(LC_ALL, "");
  notcurses_options nc_opts{};

  nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_NO_QUIT_SIGHANDLERS;

  ncpp::NotCurses nc{nc_opts};

  std::thread output_thread{BasicLeaderboardWorker{nc, running, sess}};
  std::thread input_thread{KeyWorker{nc, key_queue, running, sess, delay}};

  // terminate_latch.arrive_and_wait();
  // std::unique_lock lock(sig_cv_mtx);

  // sig_cv.wait(lock,
  //             [&shutdown_requested] { return shutdown_requested.load(); });

  int signum = 0;
  sigwait(&sigset, &signum);

  running.clear();

  input_thread.join();
  output_thread.join();

  sess.end_session();

  return EXIT_SUCCESS;
  // };

  // std::thread main_thread = std::thread(main_worker);

  // sig_thread.join();
  // main_thread.join();

  Tools::Log::Info("Exiting (graceful)...");
  std::println("Done.");
}
