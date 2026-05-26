#include <atomic>
#include <chrono>
#include <cstdlib>
#include <google/protobuf/descriptor.h>
#include <iostream>
#include <limits>
#include <ncpp/NCKey.hh>
#include <ncpp/Root.hh>
#include <notcurses/notcurses.h>
#include <optional>
#include <print>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXUserAgent.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ranges>
#include <semaphore>
#include <string>
#include <thread>

#include "ErpMessage.pb.h"
#include "session.hpp"
#include "telemetry.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_frame.hpp"
#include "time.hpp"
#include "tools/appsync_resolver.hpp"
#include "tools/base64.hpp"
#include "tools/logger.hpp"

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
  std::atomic_bool &running;
  Core::Session &sess;

  void operator()() {
    Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

    std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

    Time::Duration::DblMilliSec field_populate_duration;
    Time::TimePoint last_tick;

    Telemetry::TelemetryBoard board{};

    double render_hz = 0;

    last_tick = Time::Clock::now();

    while (running) {
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

      Tools::Log::Debug(std::format("render_duration: {}", gap.count()),
                        "LEADERBOARD");

      Tools::Log::Debug(std::format("render_hz: {}", render_hz), "LEADERBOARD");

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
  std::atomic_bool &running;
  Core::Session &sess;
  size_t &delay;
  std::binary_semaphore &end_sem;

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

  bool key_char_is(const char key, const ncinput *ni) const {
    return ((*ni->utf8 == key) && has_modifier(ni, NONE));
  }

  template <typename... Ts>
    requires(std::is_same_v<Ts, AllowedModifier> && ...)
  bool key_char_is(const char key, const ncinput *ni, const Ts... mods) const {
    return ((*ni->utf8 == key) && (has_modifier(ni, mods) && ...));
  }

  void operator()() {
    Tools::Log::Debug("Started key worker", "WORKER-INPUT");
    while (running) {
      ncinput in{};

      nc.get(true, &in);

      if (in.evtype == ncpp::EvType::Release) {
        // Release-only events
        if (key_char_is('=', &in, SHIFT)) {
          Tools::Log::Debug("Increase delay requested", "WORKER-INPUT");
          sess.set_delay_sec(++delay);
        } else if (key_char_is('-', &in)) {
          Tools::Log::Debug("Decrease delay requested", "WORKER-INPUT");
          sess.set_delay_sec(delay == 1 ? 1 : --delay);
        } else if (key_char_is('q', &in)) {
          Tools::Log::Debug("Quit requrested", "WORKER-INPUT");
          end_sem.release();
        }
      }
    }
  }
};

int main(int argc, char *argv[]) {
  Tools::Log::SetLevel(Tools::Log::DEBUG);

  for (int i = 0; i < argc; i++) {
    Tools::Log::Debug(std::format("Arg. {}: '{}'", i, argv[i]), "MAIN");
  }

  // if (strcmp(argv[1], "foo") == 0)
  //   return EXIT_SUCCESS;

  std::vector<ncpp::NCKey> key_queue{};

  std::atomic_bool running = true;
  Core::Session sess(Core::SessionSource::SERVED_DEBUG);

  size_t delay = 10;

  std::binary_semaphore end_sem{0};

  std::atomic_bool change_delay = false;
  std::atomic_size_t new_delay = delay;

  sess.set_delay_sec(delay);

  // sess.start_session();
  if (!sess.start_session())
    return EXIT_FAILURE;

  setlocale(LC_ALL, "");
  notcurses_options nc_opts{};

  nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE;

  ncpp::NotCurses nc{nc_opts};

  std::thread output_thread{BasicLeaderboardWorker{nc, running, sess}};
  // std::thread output_thread{TimeOfDayWorker{sess, running}};
  // std::thread output_thread{TimeLeftWorker{sess, running}};
  // std::thread output_thread{CrudeSpeedboardWorker{sess, running}};

  std::thread input_thread{
      KeyWorker{nc, key_queue, running, sess, delay, end_sem}};

  std::string input;

  end_sem.acquire();

  running.store(false);

  input_thread.join();
  output_thread.join();

  sess.end_session();
  Tools::Log::Info("Exiting (graceful)...");
}
