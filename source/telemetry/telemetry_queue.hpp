#pragma once

#include <atomic>
#include <expected>
#include <mutex>
#include <semaphore>
#include <shared_mutex>
#include <string_view>

#include "core/time.hpp"

namespace Telemetry {

// Forward decl.
class TelemetryFrame;

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
    } kind;
  };

private:
  // Queue methods get a shared lock (as they modify only their frame) and delay
  // reset gets a unique lock.
  std::shared_mutex _full_queue_mtx;

  // Should likely be constant, ~10 Hz, just leaving my options open
  std::atomic_size_t _refresh_hz;

  std::atomic_size_t _delay_s;

  // Set by enqueue
  std::atomic<Time::TimePoint> _newest_frame_time;

  // Set by dequeue
  std::atomic<Time::TimePoint> _oldest_frame_time;

public:
  // Delay methods
  /** @brief Retrieve the current configured delay in seconds. */
  [[nodiscard]] std::expected<size_t, Err> get_delay_sec() noexcept;

  /** @brief Retrieve the current configured refresh rate in Hertz. */
  [[nodiscard]] std::expected<size_t, Err> get_refresh_hz() noexcept;

  // Queue methods

  /** @brief Enqueue a new frame by its base64-encoded string payload. May block
   * waiting for a lock, try `try_enqueue(T) -> bool` for a non-blocking
   * conditional enqueue. Returns expected with error encountered, if any. */
  std::expected<void, Err> enqueue(std::string_view) noexcept;

  /** @brief Same as enqueue but returns expected with LOCK_FAIL error if
   * locking was not possible. */
  [[nodiscard]] std::expected<void, Err> try_enqueue(std::string_view) noexcept;

  /** @brief Same as `try_enqueue()` but only block for a specified
   * duration. */
  [[nodiscard]] std::expected<void, Err>
      try_enqueue_for(Time::Duration::DblMilliSec, std::string_view) noexcept;

  [[nodiscard]] std::expected<void, Err>
      try_enqueue_for(Time::Duration::IntMilliSec, std::string_view) noexcept;

  /** @brief Same as `try_enqueue()` but only block until a
   * specified point in time. */
  [[nodiscard]] std::expected<void, Err>
      try_enqueue_until(Time::TimePoint, std::string_view) noexcept;

  /** @brief Dequeue the next relevant frame, blocking until one is available,
   * swapping contents with the given frame. */
  std::expected<void, Err> dequeue(TelemetryFrame &) noexcept;

  /** @brief Dequeue the next relevant frame if available, swapping contents
   * with the given frame. Returns `true` if successfully dequeued, otherwise
   * `false`. */
  std::expected<void, Err> try_dequeue(TelemetryFrame &) noexcept;

  /** @brief Dequeue the next relevant frame, blocking until either one is
   * available or the specified duration passes. Swaps contents with the given
   * frame. */
  std::expected<void, Err> try_dequeue_for(Time::Duration::DblMilliSec,
                                           TelemetryFrame &) noexcept;
  std::expected<void, Err> try_dequeue_for(Time::Duration::IntMilliSec,
                                           TelemetryFrame &) noexcept;
};

}; // namespace Telemetry
