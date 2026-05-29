#pragma once

#include <cassert>
#include <deque>
#include <expected>
#include <format>
#include <memory>
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
  size_t _delay_s{0};
  size_t _refresh_hz{10};

  std::optional<size_t> _total_frames;
  std::optional<Time::Duration::DblMilliSec> _frame_period;

  std::shared_mutex _mtx;

  /** @brief Recalculate all parameters.
   *
   * Warning: Requires a unique lock on the instance's mutex and thus must not
   * be called while the mutex remains locked. */
  void recalculate() {
    std::unique_lock lock(_mtx);

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
  /** @brief Instantiate the delayinfo with the configured parameters. */
  DelayInfo(size_t delay_s, size_t refresh_hz)
      : _delay_s(delay_s), _refresh_hz(refresh_hz) {
    recalculate();
  }

  /** @brief Set a new delay length and recalculate the other parameters. */
  void set_delay_s(size_t const delay_s) {
    {
      std::unique_lock lock(_mtx);
      _delay_s = delay_s;
    }
    recalculate();
  };

  /** @brief Set a new refresh rate and recalculate the other parameters. */
  void set_refresh_hz(size_t const refresh_hz) {
    {
      std::unique_lock lock(_mtx);
      _refresh_hz = refresh_hz;
    }
    recalculate();
  }

  /** @brief Retrieve the delay in seconds. */
  [[nodiscard]] size_t get_delay_s() {
    std::shared_lock lock(_mtx);
    return _delay_s;
  }

  /** @brief Retrieve the refresh rate in Hz. */
  [[nodiscard]] size_t get_refresh_hz() {
    std::shared_lock lock(_mtx);
    return _refresh_hz;
  }

  /** @brief Retrieve the total number of delay frames required, if any. */
  [[nodiscard]] std::optional<size_t> get_delay_frames() {
    std::shared_lock lock(_mtx);
    return _total_frames;
  }

  /** @brief Retrieve the delay period in milliseconds, if any. */
  [[nodiscard]] std::optional<Time::Duration::DblMilliSec> get_frame_period() {
    std::shared_lock lock(_mtx);
    return _frame_period;
  }

  /** @brief Determine whether the delay is nonzero. */
  [[nodiscard]] bool has_delay() {
    std::shared_lock lock(_mtx);
    return _delay_s > 0;
  }
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
      ILLEGAL_DELAY,
      ILLEGAL_REFRESH,
    } kind;

    Err(Kind in_kind) : kind(in_kind) {}

    template <typename S> operator std::expected<S, Err>() const {
      return std::unexpected(kind);
    }

    operator std::string() const {
      switch (kind) {
      case TOO_RECENT:
        return "TOO_RECENT";
        break;
      case LOCK_FAIL:
        return "LOCK_FAIL";
        break;
      case DELAY_FULL:
        return "DELAY_FULL";
        break;
      case FRAME_INVALID:
        return "FRAME_INVALID";
        break;
      case ILLEGAL_DELAY:
        return "ILLEGAL_DELAY";
        break;
      case ILLEGAL_REFRESH:
        return "ILLEGAL_REFRESH";
        break;
      }
    }

    bool operator==(Err const &other) const { return kind == other.kind; }
  };

  /** @brief Constant-time constraints. May later be configurable. */
  // Longest race is around 4 hours, after that the race should be replayed
  static constexpr size_t MAX_DELAY_S = 60 * 60 * 5;
  // Messages are only sent at most around 15 Hz
  static constexpr size_t MAX_REFRESH_HZ = 20;
  // Might be able to go lower, investigate
  static constexpr size_t MIN_REFRESH_HZ = 5;

private:
  // Thread-safe
  DelayInfo _delay_info;
  std::recursive_mutex _delay_info_mtx;

  std::atomic_size_t _enq_idx;
  std::atomic_size_t _deq_idx;

  // Wrap TelemetryFrame to add a shared mutex
  struct LockedFrame {
    std::unique_ptr<TelemetryFrame> frame;
    std::mutex mtx;
  };

  std::vector<LockedFrame> _frames;
  std::shared_mutex _frame_mtx;

  /** @brief Rebuild the delay vector given a new delay configuration, copying
   * over yet-to-be-read frames. Takes a unique lock on the frame mutex. */
  void rebuild_delay() noexcept;

public:
  // Delay methods
  /** @brief Retrieve the current configured delay in seconds. */
  [[nodiscard]] std::expected<size_t, Err> get_delay_sec() noexcept;

  /** @brief Retrieve the current configured refresh rate in Hertz. */
  [[nodiscard]] std::expected<size_t, Err> get_refresh_hz() noexcept;

  /** @brief Retrieve the delay accrued by the queue in milliseconds. */
  [[nodiscard]] Time::Duration::DblMilliSec get_accrued_delay_ms() noexcept;

  /** @brief Retrieve the total number of frames in the queue currently. */
  [[nodiscard]] size_t get_total_frames() noexcept;

  /** @brief Configure the delay of the queue in seconds. */
  [[nodiscard]] std::expected<void, Err> set_delay_sec(size_t const) noexcept;

  /** @brief Configure the refresh rate of the queue in Hertz. */
  [[nodiscard]] std::expected<void, Err> set_refresh_hz(size_t const) noexcept;

  // Queue methods

  /** @brief Enqueue a new frame by its base64-encoded string payload. May block
  waiting for queue mutex. If the configured delay and current delay accrual
  does not permit adding a frame, Err::DELAY_FULL is returned. */
  std::expected<void, Err> enqueue(std::string_view const) noexcept;

  /** @brief Dequeue the next frame based on the configured delay. May block
  waiting for queue mutex. If the configured delay and current delay accrual
  does not permit dequeueing a frame, Err::TOO_RECENT is returned. */
  std::expected<std::unique_ptr<TelemetryFrame>, Err> dequeue() noexcept;

  /** @brief Instantiate the queue with a delay of 1s and a refresh rate of
   * 10hz. */
  TelemetryQueue(size_t delay_s = 2, size_t refresh_hz = 10)
      : _delay_info(DelayInfo(delay_s, refresh_hz)), _enq_idx(0), _deq_idx(0),
        _frames(std::vector<LockedFrame>(delay_s * refresh_hz)) {
    rebuild_delay();
  }
};

}; // namespace Telemetry
