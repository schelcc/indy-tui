#pragma once

#include <atomic>
#include <vector>

#include <ncpp/NotCurses.hh>

#include "core/app_context.hpp"
#include "core/core.hpp"
#include "core/session.hpp"
#include "time.hpp"

namespace Workers {

struct InterfaceWorker {
  ncpp::NotCurses &nc;
  std::atomic_flag &running;
  Core::Session &sess;

  /// @brief Maximum interface refresh rate. More than 10 Hz is likely a waste
  /// of resources, and any more than the telemetry refresh rate is certainly a
  /// waste.
  static constexpr Time::Duration::UIntMilliSec MAX_REDRAW_HZ =
      Time::Duration::UIntMilliSec(100);

  void operator()();
};

struct KeyWorker {
  ncpp::NotCurses &nc;
  std::vector<ncpp::NCKey> &key_queue;
  std::atomic_flag &running;
  Core::Session &sess;
  size_t &delay;

  static constexpr struct timespec INPUT_TIMEOUT{.tv_sec = 5, .tv_nsec = 0};

  void operator()();
};

}; // namespace Workers
