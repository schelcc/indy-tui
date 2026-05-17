#pragma once

#include "ErpMessage.pb.h"
#include "tools/base64.hpp"
#include "tools/logger.hpp"
#include "tools/timer.hpp"

#include <atomic>
#include <semaphore>
#include <string_view>

namespace Telemetry {

/** @brief Object representing a single telemetry frame, holding a
 * protobuf-generated message object, a mutex for thread-safe frame updates, and
 * a flag noting whether the frame has yet to be read. Not movable or copyable
 * due to mutex.
 */
struct TelemFrame {
  bool valid;
  proto::telemetry::ErpMessage msg;
  std::mutex mut;

  /** @brief Default-construct the frame to have a defaulted protobuf-generated
   * message with a disabled valid flag.*/
  TelemFrame() : valid(false), msg(proto::telemetry::ErpMessage{}) {}

  /** @brief Swap the underlying protobuf message with another existing messsage
   * and set the validity flag, defaulting false. Blocks according to internal
   * mutex. */
  void swap_msg(proto::telemetry::ErpMessage *in_msg,
                bool valid_after = false) {
    Tools::LoggedScopedLock lock("TelemFrame swap msg", mut);
    msg.Swap(in_msg);
    valid = valid_after;
  }

  /** @brief Populate the underlying protobuf message with a base64 encoded
   * string and set the validity flag true. Blocks according to internal mutex.
   */
  void populate(std::string_view enc_str) {
    Tools::LoggedScopedLock lock("TelemFrame populate msg", mut);
    msg.ParseFromString(Tools::b64_decode(enc_str));
    valid = true;
  }

  /** @brief Swap two TelemFrame objects, swapping their underlying
   * protobuf-generated message and validity flag. */
  static void Swap(TelemFrame &left, TelemFrame &right) {
    left.msg.Swap(&right.msg);

    bool tmp = left.valid;
    left.valid = right.valid;
    right.valid = tmp;
  }
};

/** @brief Interface for telemetry buffers. Purely virtual. */
class TelemetryBuffer {
public:
  /** @brief Populate the leading telemetry frame with the protobuf message
   * encoded in the given base64 string. Must be thread-safe, may block. */
  virtual void enqueue(std::string_view) = 0;

  /** @brief Retrieve the next telemetry frame by swapping with an existing
   * external frame. Must be thread-safe, may block. */
  virtual void dequeue(TelemFrame &) = 0;

  /** @brief Retrieve the buffer's refresh rate (Hz). */
  virtual size_t get_refresh_hz() = 0;

  /** @brief Retrieve the buffer's frame-period in milliseconds. */
  virtual size_t get_period_ms() = 0;

  /** @brief Register a semaphore to be released every period. */
  virtual void register_sem(std::shared_ptr<std::binary_semaphore>) = 0;
};

/** @brief Telemetry buffer delaying dequeue of telemetry frames based on
 * configured delay and refresh rate. Limits new frame enqueue and dequeue to
 * configured refresh rate and message availability. Thread-safe. */
class DelayBuffer : TelemetryBuffer {
  size_t _msg_rate_hz;
  size_t _delay_s;
  size_t _frame_duration_ms{0};

  // Lock for changes to msg rate or delay
  std::mutex _timing_mut{};

  size_t _num_delay_frames{0};

  std::atomic_size_t _accrued_delay_sec{0};
  std::atomic_size_t _accrued_delay_frames{0};

  std::atomic_size_t _insert_cursor = 0;
  std::atomic_size_t _read_cursor = 0;

  std::mutex _frame_mut{};
  std::vector<TelemFrame> _frames{};

  std::binary_semaphore _can_dequeue{0};

  // std::vector<std::thread> _timer_threads;
  std::vector<std::shared_ptr<std::binary_semaphore>> _reg_semaphores{};
  std::mutex _timers_mut{};

  /** @brief Recalculate and rebuild necessary buffer length, preserving yet
   * unread frames. Thread-safe, uniquely locks all access to buffer, blocking.
   */
  void recalculate_delay();

  /** @brief Block to calculate the current accrued delay values. Thread-safe,
   * non-blocking. */
  void calculate_accruals();

public:
  /** @brief Retrieve the maximum message refresh rate. Messages may populate
   * slower than this value, but never faster. Thread-safe, blocking. Non-const
   * for mutex locking. */
  [[nodiscard]] size_t get_refresh_hz() noexcept override;

  /** @brief Retrieve the buffer's frame-period in milliseconds. Thread-safe,
   * non-blocking. */
  [[nodiscard]] size_t get_period_ms() noexcept override;

  /** @brief Retrieve the configured target delay in seconds. Thread-safe,
   * blocking. Non-const for mutex locking. */
  [[nodiscard]] size_t get_delay_s() noexcept;

  /** @brief Retrieve the delay currently accrued in seconds. Thread-safe, non
   * blocking. */
  [[nodiscard]] size_t get_accrued_delay_s() const noexcept;

  /** @brief Retrieve the delay currently accrued in frames. Thread-safe, non
   * blocking. */
  [[nodiscard]] size_t get_accrued_delay_frames() const noexcept;

  /** @brief Set the maximum message refresh rate. Messages may populate slower
   * than this value, but never faster. Thread-safe, non blocking. */
  void set_refresh_hz(size_t const) noexcept;

  /** @brief Set the target delay in seconds. Thread-safe, non blocking. */
  void set_delay_s(size_t const) noexcept;

  /** @brief Populate the leading buffer frame with the protobuf message encoded
   * in the given base64 string. Only advances when the accrued delay frames
   * drops below the required frame count. Thread-safe, blocking. */
  void enqueue(std::string_view) override;

  /** @brief Retrieve the fully-delayed telemetry frame by swapping with an
   * existing frame. Blocks until a delayed frame is available. Thread-safe,
   * blocking. */
  void dequeue(TelemFrame &) noexcept override;

  /** @brief TODO */
  void register_sem(std::shared_ptr<std::binary_semaphore>) override;

  DelayBuffer(size_t refresh_hz, size_t delay_s)
      : _msg_rate_hz(refresh_hz), _delay_s(delay_s) {
    recalculate_delay();
  }
};

}; // namespace Telemetry
