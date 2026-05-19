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
#include "telemetry.hpp"
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
  }
}

struct TelemInfo {
  std::string car_num;
  std::string driver_name;

  proto::telemetry::ErpTelemetry telem{};
  proto::telemetry::ErpOverallResults results{};

  std::string get_line() const {
    int rank = results.has_overallrank() ? results.overallrank() : 0;

    std::string name =
        (results.has_firstname() ? results.firstname() : "Driver") + " " +
        (results.has_lastname() ? results.lastname() : "Name");

    std::string_view num = results.has_carnumber() ? results.carnumber() : "00";

    double vehicle_speed =
        telem.has_vehiclespeed() ? telem.vehiclespeed() : 0.0;

    double throttle_pct = telem.has_throttle() ? telem.throttle() : 0.0;
    double brake_pct =
        telem.has_breakpercentage() ? telem.breakpercentage() : 0.0;

    std::string_view warmupspeed =
        results.has_warmupqualspeed() ? results.warmupqualspeed() : "000.000";
    std::string_view lap1speed =
        results.has_lap1qualspeed() ? results.lap1qualspeed() : "000.000";
    std::string_view lap2speed =
        results.has_lap2qualspeed() ? results.lap2qualspeed() : "000.000";
    std::string_view lap3speed =
        results.has_lap3qualspeed() ? results.lap3qualspeed() : "000.000";
    std::string_view lap4speed =
        results.has_lap4qualspeed() ? results.lap4qualspeed() : "000.000";
    std::string_view averagespeed =
        results.has_averagespeed() ? results.averagespeed() : "000.000";

    return std::format(
        ". {:>20} #{:<2} | {: >8.2f} MPH | {:>6.1f}% | "
        "{:>6.1f}% | "
        "{:>7} MPH | {:>7} MPH | {:>7} MPH | {:>7} MPH | {:>7} "
        "MPH | {:>7} MPH |                                                    ",
        name, num, vehicle_speed, throttle_pct, brake_pct, warmupspeed,
        lap1speed, lap2speed, lap3speed, lap4speed, averagespeed);
  }
};

struct BasicLeaderboardWorker {
  ncpp::NotCurses &nc;
  std::atomic_bool &running;
  Telemetry::TelemetrySession &sess;

  void operator()() {
    Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

    proto::telemetry::ErpMessage telem_frame{};
    std::string time_of_day{"00:00:00"};

    std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

    // std::shared_ptr<ncpp::Plane> leaderboard_plane(std_plane);

    std::unordered_map<std::string, size_t> info_cache{};
    std::vector<TelemInfo> info_vec{};

    std::chrono::duration<double, std::ratio<1, 1000>> field_populate_duration;

    std::chrono::steady_clock::time_point last_tick;

    bool did_field_render = false;

    double render_hz = 0;

    auto switched_speed_str = [](std::string_view s) {
      return (s.size() > 0) ? s : "000.000";
    };

    last_tick = std::chrono::steady_clock::now();

    while (running) {
      auto render_start = std::chrono::steady_clock::now();

      size_t row = 1;

      auto block_time = sess.get_block_time();

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
          std::format("Accrued delay: {}s\t\t\t\t\t", sess.accrued_delay())
              .c_str());

      std_plane->putstr(row++, 0,
                        std::format("Accrued delay frames: {} frames",
                                    sess.accrued_delay_frames())
                            .c_str());

      std_plane->putstr(row++, 0,
                        std::format("Raw message recv. rate: {:.2f} Hz\t\t\t\t",
                                    sess.get_recv_hz())
                            .c_str());

      std_plane->putstr(row++, 0,
                        std::format("Message enqueue rate: {:.2f} Hz\t\t\t\t",
                                    sess.get_enq_hz())
                            .c_str());
      // std_plane->putstr(row++, 0,
      //                   std::format("Log write rate: {:.2f} Hz\t\t\t\t",
      //                               Tools::Log::GetWriteRate())
      //                       .c_str());

      std_plane->putstr(row++, 0, "----------------------------");

      auto res = sess.swap_telemetry(std::move(telem_frame));

      // Info-cache populate
      auto field_populate_start = std::chrono::steady_clock::now();

      for (auto iter = telem_frame.mutable_overallresults()->begin();
           iter < telem_frame.mutable_overallresults()->end(); ++iter) {
        if (!iter->IsInitialized())
          continue;
        std::string_view car_num{iter->carnumber()};

        if (!info_cache.contains(car_num.data())) {
          info_cache[car_num.data()] = info_vec.size();
          info_vec.emplace_back(TelemInfo{});
        }

        info_vec.at(info_cache.at(car_num.data())).results.Swap(&*iter);
      }
      for (auto iter = telem_frame.mutable_telemetrymessages()->begin();
           iter < telem_frame.mutable_telemetrymessages()->end(); ++iter) {
        if (!iter->IsInitialized())
          continue;
        std::string_view car_num{iter->carnumber()};
        if (!info_cache.contains(car_num.data())) {
          info_cache[car_num.data()] = info_vec.size();
          info_vec.emplace_back(TelemInfo{});
        }

        info_vec.at(info_cache.at(car_num.data())).telem.Swap(&*iter);
      }

      field_populate_duration =
          std::chrono::steady_clock::now() - field_populate_start;

      row++;

      // Draw leaderboard
      did_field_render = false;
      std::sort(std::begin(info_vec), std::end(info_vec),
                [](TelemInfo const &a, TelemInfo const &b) {
                  // return a.telem.vehiclespeed() < b.telem.vehiclespeed();
                  return std::stod(a.results.has_averagespeed()
                                       ? a.results.averagespeed()
                                       : "0.0") <
                         std::stod(b.results.has_averagespeed()
                                       ? b.results.averagespeed()
                                       : "0.0");
                  // return a.results.overallrank() < b.results.overallrank();
                });

      size_t rank = 0;
      for (auto const &telem_row : info_vec) {
        // std_plane->putstr(row++, 0, (std::string{++rank} +
        // telem_row.get_line()).c_str());
        std_plane->putstr(
            row++, 0,
            std::format("{}.{}", ++rank, telem_row.get_line()).c_str());
      }

      nc.render();

      std_plane->erase();

      std::this_thread::sleep_until(block_time);

      std::chrono::duration<double, std::ratio<1, 1000>> gap =
          (std::chrono::steady_clock::now() - render_start);

      render_hz = 1000.0 / gap.count();

      Tools::Log::Debug(std::format("render_duration: {}", gap.count()),
                        "LEADERBOARD");

      Tools::Log::Debug(std::format("render_hz: {}", render_hz), "LEADERBOARD");

      last_tick = std::chrono::steady_clock::now();
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
  Telemetry::TelemetrySession &sess;
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
      uint32_t key_code = nc.get(true, &in);

      if (in.evtype == ncpp::EvType::Release) {
        // Release-only events
        if (key_char_is('=', &in, SHIFT)) {
          Tools::Log::Debug("Increase delay requested", "WORKER-INPUT");
          sess.set_delay(++delay);
        } else if (key_char_is('-', &in)) {
          Tools::Log::Debug("Decrease delay requested", "WORKER-INPUT");
          sess.set_delay(delay == 1 ? 1 : --delay);
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
  Telemetry::TelemetrySession sess{};

  size_t delay = 2;
  size_t refresh = 10;

  std::binary_semaphore end_sem{0};

  std::atomic_bool change_delay = false;
  std::atomic_size_t new_delay = delay;

  sess.set_refresh(refresh);
  sess.set_delay(delay);

  // sess.start_session();
  sess.start_served_replay_session();

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
