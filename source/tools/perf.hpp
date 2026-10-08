#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <mutex>
#include <utility>

#include "core/time.hpp"

#include "tools/logger.hpp"

namespace App {

namespace Perf {

constexpr size_t PERF_WINDOW_LEN = 8;

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

  GateToGate() {
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

  SingleShot() {
    data.fill(std::numeric_limits<Time::Duration::DblMilliSec>::min());
  }
};

}; // namespace Perf

#define PERF_ASPECT_SS(name)                                                   \
  Perf::SingleShot _##name{};                                                  \
  void rec_##name(Time::Duration::DblMilliSec &&t) {                           \
    std::scoped_lock lock(mtx_);                                               \
    _##name.collect(std::move(t));                                             \
  }                                                                            \
  static void Rec##name(Time::Duration::DblMilliSec &&t) {                     \
    Get().rec_##name(std::move(t));                                            \
  }                                                                            \
  static void Log##name() {                                                    \
    Tools::Log::Debug(std::format("Perf Stat - {}: {} / {} samples", #name,    \
                                  Get()._##name.avg(), Perf::PERF_WINDOW_LEN), \
                      "PERF");                                                 \
  }

#define PERF_ASPECT_G2G(name)                                                  \
  Perf::GateToGate _##name{};                                                  \
  void start_##name() { _##name.start(); }                                     \
  void stop_##name() { _##name.stop(); }                                       \
  static void Start##name() { Get().start_##name(); }                          \
  static void Stop##name() { Get().stop_##name(); }                            \
  static void Log##name() {                                                    \
    Tools::Log::Debug(std::format("Perf Stat - {}: {} / {} samples", #name,    \
                                  Get()._##name.avg(), Perf::PERF_WINDOW_LEN), \
                      "PERF");                                                 \
  }

class PerfContext {
  std::mutex mtx_;

public:
  static PerfContext &Get() {
    static PerfContext perf{};
    return perf;
  }

  PERF_ASPECT_SS(KeyHandleTime);

  PERF_ASPECT_G2G(LeaderboardPopulationTime);
  PERF_ASPECT_G2G(RenderTime);

  PerfContext() = default;
  ~PerfContext() = default;

  PerfContext(PerfContext const &) = delete;
  PerfContext &operator=(PerfContext const &) = delete;
  PerfContext(PerfContext &&) = delete;
  PerfContext &operator=(PerfContext &&) = delete;
};

}; // namespace App
