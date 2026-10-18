#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <mutex>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <stop_token>
#include <thread>
#include <utility>

#include "core/app_context.hpp"
#include "core/time.hpp"

namespace App {

namespace Perf {

constexpr size_t PERF_WINDOW_LEN = 64;

template <size_t... Idxs>
Time::Duration::DblMilliSec unrolled_avg(
    std::array<Time::Duration::DblMilliSec, PERF_WINDOW_LEN> const &arr,
    std::index_sequence<Idxs...>) {
  return Time::Duration::DblMilliSec((arr[Idxs].count() + ...) /
                                     sizeof...(Idxs));
}

static inline Time::Duration::DblMilliSec unrolled_avg(
    std::array<Time::Duration::DblMilliSec, PERF_WINDOW_LEN> const &arr) {
  return unrolled_avg(arr,
                      std::make_integer_sequence<size_t, PERF_WINDOW_LEN>());
}

static std::vector<std::function<void(spdlog::logger *)>> &PerfLogCalls() {
  static std::vector<std::function<void(spdlog::logger *)>> v{};
  return v;
}

struct GateToGate {
  mutable std::mutex mtx;
  std::array<Time::Duration::DblMilliSec, PERF_WINDOW_LEN> data{};
  std::atomic_size_t pos = 0;

  Time::TimePoint start_t;

  void start() {
    std::scoped_lock lock(mtx);
    start_t = Time::Clock::now();
  }

  void stop() {
    std::scoped_lock lock(mtx);
    data[pos.fetch_add(1) % PERF_WINDOW_LEN] = Time::Clock::now() - start_t;
  }

  Time::Duration::DblMilliSec avg() {
    std::scoped_lock lock(mtx);
    return unrolled_avg(data);
  }

  GateToGate(std::string &&name) {
    auto log_call = [this](std::string_view n, spdlog::logger *l) {
      l->info("{},{:.4f} ms,{}", n, avg().count(), PERF_WINDOW_LEN);
    };
    PerfLogCalls().emplace_back(
        [name = std::move(name), f = std::move(log_call)](spdlog::logger *l) {
          f(name, l);
        });
    data.fill(std::numeric_limits<Time::Duration::DblMilliSec>::min());
  }
};

struct SingleShot {
  mutable std::mutex mtx;
  std::array<Time::Duration::DblMilliSec, PERF_WINDOW_LEN> data{};
  std::atomic_size_t pos = 0;

  void collect(Time::Duration::DblMilliSec &&t) {
    std::scoped_lock lock(mtx);
    data[pos.fetch_add(1) % PERF_WINDOW_LEN] = t;
  }

  Time::Duration::DblMilliSec avg() {
    std::scoped_lock lock(mtx);
    return unrolled_avg(data);
  }

  SingleShot(std::string &&name) {
    auto log_call = [this](std::string_view n, spdlog::logger *l) {
      l->info("{},{:.4f} ms,{}", n, avg().count(), PERF_WINDOW_LEN);
    };
    PerfLogCalls().emplace_back(
        [name = std::move(name), f = std::move(log_call)](spdlog::logger *l) {
          f(name, l);
        });
    data.fill(std::numeric_limits<Time::Duration::DblMilliSec>::min());
  }
};

}; // namespace Perf

#define PERF_ASPECT_SS(name)                                                   \
  Perf::SingleShot _##name{#name};                                             \
  void rec_##name(Time::Duration::DblMilliSec &&t) {                           \
    std::scoped_lock lock(mtx_);                                               \
    _##name.collect(std::move(t));                                             \
  }                                                                            \
  static void Rec##name(Time::Duration::DblMilliSec &&t) {                     \
    Get().rec_##name(std::move(t));                                            \
  }

#define PERF_ASPECT_G2G(name)                                                  \
  Perf::GateToGate _##name{#name};                                             \
  void start_##name() { _##name.start(); }                                     \
  void stop_##name() { _##name.stop(); }                                       \
  static void Start##name() { Get().start_##name(); }                          \
  static void Stop##name() { Get().stop_##name(); }

class PerfContext {
  std::mutex mtx_;

  std::shared_ptr<spdlog::logger> logger_;

  std::jthread logging_thread_;
  static constexpr Time::Duration::UIntMilliSec PERF_LOG_PERIOD{250ul};

public:
  static PerfContext &Get() {
    static PerfContext perf{};
    return perf;
  }

  PERF_ASPECT_SS(KeyHandleTime);

  PERF_ASPECT_G2G(LeaderboardPopulationTime);
  PERF_ASPECT_G2G(RenderTime);
  PERF_ASPECT_G2G(InformBoardTime);

  PerfContext() {
    try {
      logger_ = spdlog::basic_logger_mt(
          "perf_logger", std::format("logs/perf/{}.log",
                                     App::AppContext::InstanceName().data()));
      logger_->set_pattern("%H:%M:%S:%e,%v");
    } catch (const spdlog::spdlog_ex &ex) {
      std::println("Perf log init failed: {}", ex.what());
      std::terminate();
    }

    logging_thread_ =
        std::jthread{[logger = logger_](std::stop_token stop_tok) {
          auto last_tick = Time::Clock::now();
          Time::TimePoint block_until;
          while (!stop_tok.stop_requested()) {
            block_until = last_tick + PERF_LOG_PERIOD;
            for (auto &f : Perf::PerfLogCalls())
              f(logger.get());
            std::this_thread::sleep_until(block_until);
            last_tick = Time::Clock::now();
          }
        }};
  }

  ~PerfContext() = default;

  PerfContext(PerfContext const &) = delete;
  PerfContext &operator=(PerfContext const &) = delete;
  PerfContext(PerfContext &&) = delete;
  PerfContext &operator=(PerfContext &&) = delete;
};

}; // namespace App
