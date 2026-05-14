#include <chrono>
#include <cstdlib>
#include <iostream>
#include <print>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXUserAgent.h>
#include <ixwebsocket/IXWebSocket.h>
#include <thread>

#include "ErpMessage.pb.h"
#include "telemetry.hpp"
#include "tools/appsync_resolver.hpp"
#include "tools/base64.hpp"
#include "tools/logger.hpp"

using Tools::AppSyncSession;

using std::chrono::duration_cast;
using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;

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

int main() {
  Tools::Log::SetLevel(Tools::Log::DEBUG);
  std::atomic_bool running = true;
  Telemetry::TelemetrySession sess{};

  size_t delay = 20;
  size_t refresh = 10;

  sess.set_refresh(refresh);
  sess.set_delay(delay);

  // sess.start_session();
  sess.start_served_replay_session();

  std::thread output_thread{TimeOfDayWorker{sess, running}};
  // std::thread output_thread{TimeLeftWorker{sess, running}};
  // std::thread output_thread{CrudeSpeedboardWorker{sess, running}};

  std::string input;

  while (std::getline(std::cin, input)) {
    Tools::Log::Info("Main loop iteration", "MAIN_THREAD");
    if (input.length() != 2)
      continue;

    if (input.at(0) == 'd')
      sess.set_delay((input.at(1) == '+') ? ++delay : --delay);
    if (input.at(0) == 'r')
      sess.set_refresh((input.at(1) == '+') ? ++refresh : --refresh);
  }

  running.store(false);
  sess.end_session();
  output_thread.join();
  Tools::Log::Info("Exiting (graceful)...");
}
