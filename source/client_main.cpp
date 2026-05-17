#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <print>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXUserAgent.h>
#include <ixwebsocket/IXWebSocket.h>
#include <semaphore>
#include <thread>

#include "ErpMessage.pb.h"
#include "telemetry.hpp"
#include "telemetry_buffer.hpp"
#include "tools/appsync_resolver.hpp"
#include "tools/base64.hpp"
#include "tools/logger.hpp"

#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>

using Tools::AppSyncSession;

using std::chrono::duration_cast;
using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;

// struct BasicLeaderboardWorker {
//   ncpp::NotCurses &nc;
//   std::atomic_bool &running;
//   Telemetry::TelemetrySession &sess;

//   void operator()() {
//     proto::telemetry::ErpMessage telem_frame{};
//     std::string time_of_day{"00:00:00"};

//     std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());
//     // std::shared_ptr<ncpp::Plane> leaderboard_plane(std_plane);

//     while (running.load()) {
//       size_t row = 0;

//       auto block_time = sess.get_block_time();

//       std_plane->putstr(
//           row++, 0,
//           std::format("Accrued delay: {}", sess.accrued_delay()).c_str());

//       std_plane->putstr(
//           row++, 0,
//           std::format("Accrued delay frames: {}",
//           sess.accrued_delay_frames())
//               .c_str());

//       auto res = sess.swap_telemetry(std::move(telem_frame));

//       if ((telem_frame.heartbeats_size() > 0) &&
//           (telem_frame.heartbeats().Get(0).has_timeofday()))
//         time_of_day = telem_frame.heartbeats().Get(0).timeofday();

//       for (auto &msg : telem_frame.telemetrymessages()) {
//         std_plane->putstr(row++, 0,
//                           std::format("Car #{}  :  Speed[{}]",
//                           msg.carnumber(),
//                                       msg.vehiclespeed())
//                               .c_str());
//       }

//       std_plane->putstr(row++, 0, "----------------------------");
//       std_plane->putstr(row++, 0,
//                         std::format("Time of day: {}", time_of_day).c_str());

//       nc.render();

//       std::this_thread::sleep_until(block_time);
//     }
//   }
// };

struct TimeOfDayWorker {
  std::shared_ptr<Telemetry::ServedSession> sess;
  std::atomic_bool &running;

  void operator()() {
    proto::telemetry::ErpMessage local_frame{};
    Telemetry::TelemFrame frame{};

    std::string telem_dbg;

    while (running.load()) {

      sess->swap_telemetry(frame);
      frame.swap_msg(&local_frame);

      if (local_frame.heartbeats_size() > 0) {
        auto &heartbeat = local_frame.heartbeats().Get(0);

        if (heartbeat.has_timeofday()) {
          Tools::Log::Info(
              std::format("\t\tTime of day: {}", heartbeat.timeofday()),
              "OUTPUT");
        }
      }
    }
  }
};

// struct TimeLeftWorker {
//   Telemetry::TelemetrySession &sess;
//   std::atomic_bool &running;

//   void operator()() {
//     proto::telemetry::ErpMessage telem_frame;
//     std::string telem_dbg;
//     while (running.load()) {
//       auto block_time = sess.get_block_time();

//       if (sess.ripe()) {
//         auto res = sess.swap_telemetry(std::move(telem_frame));

//         if (telem_frame.heartbeats_size() > 0) {
//           auto heartbeat = telem_frame.heartbeats().Get(0);

//           if (heartbeat.has_overalltimetogo()) {

//             std::div_t min_div =
//                 std::div(std::stoi(heartbeat.overalltimetogo()), 60);
//             std::div_t hr_div = std::div(min_div.quot, 60);

//             Tools::Log::Debug(std::format("\t\tTime to go: {}:{}:{}",
//                                           hr_div.quot, hr_div.rem,
//                                           min_div.rem),
//                               "OUTPUT");
//           }
//         }
//       }

//       std::this_thread::sleep_until(block_time);
//     }
//   }
// };

// struct CrudeSpeedboardWorker {
//   Telemetry::TelemetrySession &sess;
//   std::atomic_bool &running;

//   void operator()() {
//     proto::telemetry::ErpMessage telem_frame;
//     while (running.load()) {
//       auto block_time = sess.get_block_time();

//       if (sess.ripe()) {
//         auto res = sess.swap_telemetry(std::move(telem_frame));
//         printf("\033[H\033[J");

//         for (auto const &row : telem_frame.telemetrymessages()) {
//           std::println("#{}   SPEED[{} MPH]  ENGINE[{} RPM]  GEAR[{}]",
//                        row.carnumber(), row.vehiclespeed(),
//                        row.enginespeed(), row.gear());
//         }
//         if (telem_frame.heartbeats_size() > 0) {
//           auto heartbeat = telem_frame.heartbeats().Get(0);

//           if (heartbeat.has_overalltimetogo()) {

//             std::div_t min_div =
//                 std::div(std::stoi(heartbeat.overalltimetogo()), 60);
//             std::div_t hr_div = std::div(min_div.quot, 60);

//             std::println("\t\tTime to go: {}:{}:{}", hr_div.quot, hr_div.rem,
//                          min_div.rem);
//           }
//         }
//       }

//       std::this_thread::sleep_until(block_time);
//     }
//   }
// };

int main() {
  Tools::Log::SetLevel(Tools::Log::NONE);
  std::atomic_bool running = true;

  std::shared_ptr<Telemetry::TelemetryBuffer> buf =
      std::make_shared<Telemetry::DelayBuffer>(10, 10);
  // std::shared_ptr<Telemetry::TelemetryBuffer> buf(10, 10);
  // Telemetry::DelayBuffer buf(10, 10);

  Telemetry::LocalServedReplay sess(buf, "127.0.0.1", "8080");

  size_t delay = 2;
  size_t refresh = 10;

  sess.set_refresh(refresh);
  sess.set_delay(delay);

  // sess.start_session();
  sess.start_served_replay_session();

  setlocale(LC_ALL, "");
  notcurses_options nc_opts{};

  nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_DRAIN_INPUT;

  ncpp::NotCurses nc{nc_opts};

  std::thread output_thread{BasicLeaderboardWorker{nc, running, sess}};
  output_thread.join();
  // std::thread output_thread{TimeOfDayWorker{sess, running}};
  // std::thread output_thread{TimeLeftWorker{sess, running}};
  // std::thread output_thread{CrudeSpeedboardWorker{sess, running}};

  // std::string input;

  // while (std::getline(std::cin, input)) {
  //   Tools::Log::Info("Main loop iteration", "MAIN_THREAD");
  //   if (input.length() != 2)
  //     continue;

  //   if (input.at(0) == 'd')
  //     sess.set_delay((input.at(1) == '+') ? ++delay : --delay);
  //   if (input.at(0) == 'r')
  //     sess.set_refresh((input.at(1) == '+') ? ++refresh : --refresh);
  // }

  running.store(false);
  sess.end_session();
  output_thread.join();
  Tools::Log::Info("Exiting (graceful)...");
}
