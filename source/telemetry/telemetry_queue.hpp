#pragma once

#include <cassert>
#include <deque>
#include <expected>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string_view>

#include "core/time.hpp"
#include "logger.hpp"
#include "telemetry/telemetry_frame.hpp"

namespace Telemetry {

/** @brief A simple thread-safe dataclass to group delay information like
 * refresh rate, delay length, and frame period. */
class DelayInfo {
  size_t _delay_s;
  size_t _refresh_hz;

  std::optional<size_t> _total_frames;
  std::optional<Time::Duration::DblMilliSec> _frame_period;

  std::recursive_mutex _mtx;

  /** @brief Recalculate all parameters. */
  void recalculate() {
    Tools::LoggedScopedLock lock("recalc - DelayInfo Mutex", _mtx);

    assert(_refresh_hz > 0);

    if (_delay_s > 0) {
      _total_frames = _delay_s * _refresh_hz;
      _frame_period = Time::Duration::DblMilliSec(
          static_cast<double>(_refresh_hz) / 1000.0);
    } else {
      _total_frames = {};
      _frame_period = {};
    }
  }

public:
  /** @brief Set a new delay length and recalculate the other parameters. */
  void set_delay_s(size_t const delay_s) {
    Tools::LoggedScopedLock lock("set_delay - DelayInfo Mutex", _mtx);
    _delay_s = delay_s;
    recalculate();
  };

  /** @brief Set a new refresh rate and recalculate the other parameters. */
  void set_refresh_hz(size_t const refresh_hz) {
    Tools::LoggedScopedLock lock("set_refresh_hz - DelayInfo Mutex", _mtx);
    _refresh_hz = refresh_hz;
    recalculate();
  }

  /** @brief Retrieve the delay in seconds. */
  [[nodiscard]] size_t get_delay_s() {
    Tools::LoggedScopedLock lock("get_delay_s - DelayInfo Mutex", _mtx);
    return _delay_s;
  }

  /** @brief Retrieve the refresh rate in Hz. */
  [[nodiscard]] size_t get_refresh_hz() {
    Tools::LoggedScopedLock lock("get_delay_s - DelayInfo Mutex", _mtx);
    return _refresh_hz;
  }

  /** @brief Retrieve the total number of delay frames required, if any. */
  [[nodiscard]] std::optional<size_t> get_delay_frames() {
    Tools::LoggedScopedLock lock("get_delay_frames - DelayInfo Mutex", _mtx);
    return _total_frames;
  }

  /** @brief Retrieve the delay period in milliseconds, if any. */
  [[nodiscard]] std::optional<Time::Duration::DblMilliSec> get_frame_peiod() {
    Tools::LoggedScopedLock lock("get_delay_frames - DelayInfo Mutex", _mtx);
    return _frame_period;
  }

  /** @brief Determine whether the delay is nonzero. */
  [[nodiscard]] bool has_delay() {
    Tools::LoggedScopedLock lock("get_delay_frames - DelayInfo Mutex", _mtx);
    return _delay_s > 0;
  }

  /** @brief Retrieve the DelayInfo's mutex to allow external users to lock
   * behavior with it. */
  [[nodiscard]] std::recursive_mutex &get_mtx() { return _mtx; }
};

/** @brief Thread-safe live & delayable telemetry queue. */
class TelemetryQueue {
public:
  /** @brief `TelemetryQueue` Error type.
   *
   * Errors:
   *  - TOO_RECENT : The frame to be dequeued is too recent for the configured
   * delay. */
  struct Err {
    enum Kind {
      TOO_RECENT,
      LOCK_FAIL,
      DELAY_FULL,
      FRAME_INVALID,
    } kind;
  };

private:
  // Queue methods get a shared lock (as they modify only their frame) and
  // delay reset gets a unique lock.
  std::shared_mutex _full_queue_mtx;

  DelayInfo _delay_info;

  std::queue<TelemetryFrame, std::deque<TelemetryFrame>> _frame_queue;

  std::string _current_frame;

public:
  // Delay methods
  /** @brief Retrieve the current configured delay in seconds. */
  [[nodiscard]] std::expected<size_t, Err> get_delay_sec() noexcept;

  /** @brief Retrieve the current configured refresh rate in Hertz. */
  [[nodiscard]] std::expected<size_t, Err> get_refresh_hz() noexcept;

  [[nodiscard]] Time::Duration::DblMilliSec get_accrued_delay_ms() noexcept;

  void set_delay_sec(size_t const) noexcept;

  // Queue methods

  /** @brief Enqueue a new frame by its base64-encoded string payload. May block
  waiting for queue mutex. If the configured delay and current delay accrual
  does not permit adding a frame, Err::DELAY_FULL is returned. */
  std::expected<void, Err> enqueue(std::string_view const) noexcept;

  /** @brief Dequeue the next frame based on the configured delay. May block
  waiting for queue mutex. If the configured delay and current delay accrual
  does not permit dequeueing a frame, Err::TOO_RECENT is returned. */
  std::expected<TelemetryFrame, Err> dequeue() noexcept;
};

}; // namespace Telemetry
