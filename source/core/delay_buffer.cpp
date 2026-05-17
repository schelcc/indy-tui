#include "core/telemetry_buffer.hpp"
#include "tools/logger.hpp"
#include <string_view>

using Tools::Log;
using Tools::LoggedSharedLock;
using Tools::LoggedUniqueLock;

namespace Telemetry {

/** @brief Recalculate and rebuild necessary buffer length, preserving yet
 * unread frames. Thread-safe, uniquely locks all access to buffer, blocking.
 *
 * Grows or shrinks the underlying vec of frames, "copy"ing yet-to-be-read
 * frames between the read and insert cursors.
 */
void DelayBuffer::recalculate_delay() {
  Log::Info("Recalculate delay", "DELAYBUF");
  LoggedUniqueLock frame_lock("recalculate_delay (frame lock)", _frame_mut);

  size_t old_delay_frames;
  size_t new_delay_frames;
  {
    LoggedUniqueLock timing_lock("recalculate_delay (timing lock)",
                                 _timing_mut);

    old_delay_frames = _num_delay_frames;
    new_delay_frames = _msg_rate_hz * _delay_s;

    _frame_duration_ms = 1000 / _msg_rate_hz;

    _num_delay_frames = new_delay_frames;

    assert(old_delay_frames > 0);
    assert(_num_delay_frames > 0);

    // Advance read cursor as far as required (none if we're growing)
    _read_cursor += (_num_delay_frames < old_delay_frames)
                        ? (old_delay_frames - _num_delay_frames)
                        : 0;
  }

  // Populate new vec
  std::vector<TelemFrame> new_frames(new_delay_frames);

  Log::Debug("Copy over yet-to-be-read frames", "DELAYBUF");
  for (size_t copy_cursor = _read_cursor; copy_cursor < _insert_cursor;
       ++copy_cursor) {
    TelemFrame::Swap(new_frames.at(copy_cursor % new_delay_frames),
                     _frames.at(copy_cursor % old_delay_frames));
  }

  Log::Debug("Swap over new frame vec", "DELAYBUF");
  _frames.swap(new_frames);
}

/** @brief Block to calculate the current accrued delay values. Thread-safe,
 * non-blocking. */
void DelayBuffer::calculate_accruals() {
  Log::Debug("Re-calculate delay accrual", "DELAYBUF");
  LoggedSharedLock timing_lock("calculate_accruals (timing lock)", _timing_mut);

  assert(_msg_rate_hz > 0);

  _accrued_delay_frames =
      (_read_cursor > _insert_cursor) ? 0 : _insert_cursor - _read_cursor;
  _accrued_delay_sec =
      (_accrued_delay_frames == 0) ? 0 : (_accrued_delay_frames / _msg_rate_hz);
}

/** @brief Retrieve the maximum message refresh rate. Messages may populate
 * slower than this value, but never faster. Thread-safe, blocking.*/
[[nodiscard]] size_t DelayBuffer::get_refresh_hz() noexcept {
  LoggedSharedLock timing_lock("get_refresh_hz (timing lock)", _timing_mut);
  return _msg_rate_hz;
}

/** @brief Retrieve the buffer's frame duration in milliseconds. Thread-safe,
 * blocking. */
[[nodiscard]] size_t DelayBuffer::get_period_ms() noexcept {
  LoggedSharedLock timing_lock("get_period_ms (timing lock)", _timing_mut);
  return _frame_duration_ms;
}

/** @brief Retrieve the configured target delay in seconds. Thread-safe,
 * blocking. */
[[nodiscard]] size_t DelayBuffer::get_delay_s() noexcept {
  LoggedSharedLock timing_lock("get_delay_s (timing lock)", _timing_mut);
  return _msg_rate_hz;
}

/** @brief Retrieve the delay currently accrued in seconds. Thread-safe, non
 * blocking. */
[[nodiscard]] size_t DelayBuffer::get_accrued_delay_s() const noexcept {
  return _accrued_delay_sec;
}

/** @brief Retrieve the delay currently accrued in frames. Thread-safe, non
 * blocking. */
[[nodiscard]] size_t DelayBuffer::get_accrued_delay_frames() const noexcept {
  return _accrued_delay_frames;
}

/** @brief Populate the leading buffer frame with the protobuf message encoded
 * in the given base64 string. Only advances when the accrued delay frames
 * drops below the required frame count. Thread-safe, blocking. */
void DelayBuffer::enqueue(std::string_view payload) {
  Log::Debug("Enqueue new frame", "DELAYBUF");
  {
    LoggedSharedLock frame_lock("enqueue (frames)", _frame_mut);
    _frames.at(_insert_cursor % _frames.size()).populate(payload);
  }

  calculate_accruals();

  {
    LoggedSharedLock timing_lock("enqueue (timing)", _timing_mut);
    if (_accrued_delay_frames < _num_delay_frames) {
      Log::Debug("Advance insertion cursor", "DELAYBUF");
      _insert_cursor++;
      _can_dequeue.release();
    }
  }
}

/** @brief Retrieve the fully-delayed telemetry frame by swapping with an
 * existing frame. Blocks until a delayed frame is available. Thread-safe,
 * blocking. */
void DelayBuffer::dequeue(TelemFrame &frame) noexcept {
  // Blocks until we're good to dequeue
  Log::Debug("Dequeue frame", "DELAYBUF");
  _can_dequeue.acquire();

  LoggedSharedLock frame_lock("dequeue (frames)", _frame_mut);

  assert(_frames.at(_read_cursor % _frames.size()).valid);
  _frames.at(_read_cursor % _frames.size()).swap_msg(&frame.msg);
  _read_cursor++;
}

} // namespace Telemetry
