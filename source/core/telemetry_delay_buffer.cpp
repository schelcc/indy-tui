#include "appsync_resolver.hpp"
#include "core/telemetry.hpp"
#include "ixwebsocket/IXWebSocketMessage.h"
#include "tools/logger.hpp"
#include <mutex>
#include <print>

namespace Telemetry {

[[nodiscard]] TelemetryDelayBuffer::DelayStatus
TelemetryDelayBuffer::get_delay_status() {
  Tools::Log::Debug(
      std::format("Accrued delay: {}s / {}s | Frame gap: {} | Total frames: {}",
                  accrued_delay(), _delay_s.load(),
                  _insert_cursor - _read_cursor, _delay_frames.load()));

  Tools::LoggedScopedLock lock("get_delay_status (deq, enq)", _deq_mut,
                               _enq_mut);

  if (_read_cursor > _insert_cursor)
    return DelayStatus::ERROR;

  size_t diff = _insert_cursor - _read_cursor;
  if (diff < _delay_frames.load())
    return DelayStatus::UNDER;
  else if (diff > _delay_frames.load())
    return DelayStatus::OVER;
  else
    return DelayStatus::EXACT;
}

void TelemetryDelayBuffer::recalculate_delay() {
  size_t new_delay_frames = _max_msgs_per_sec.load() * _delay_s.load();
  size_t prev_delay_frames = _delay_frames.exchange(new_delay_frames);

  // Calculate the num of frames we need to jump forward if we're shrinking
  size_t skip_frames = (new_delay_frames < prev_delay_frames)
                           ? prev_delay_frames - new_delay_frames
                           : 0;

  std::vector<Frame> new_frames(new_delay_frames);

  {
    // If we get this lock we are guaranteed no one is accessing the frames vec
    Tools::LoggedScopedLock lock("recalculate_delay() (enq and deq)", _enq_mut,
                                 _deq_mut);

    // Advance read cursor as far as required (none if we're growing)
    _read_cursor += skip_frames;

    if (prev_delay_frames > 0) {
      size_t new_idx;
      size_t old_idx;

      // Copy frames we still want to get to
      for (size_t copy_cursor = _read_cursor; copy_cursor < _insert_cursor;
           ++copy_cursor) {
        new_idx = copy_cursor % new_delay_frames;
        old_idx = copy_cursor % prev_delay_frames;

        Frame &new_frame = new_frames.at(new_idx);
        Frame &old_frame = _frames.at(old_idx);

        new_frame.swap(std::move(old_frame.msg));
        new_frame.valid = old_frame.valid;
      }
    }

    _frames = std::move(new_frames);
  }
}

std::expected<void, TelemetryErr>
TelemetryDelayBuffer::enqueue(std::string_view enc_str) {
  Tools::Log::Debug("Get delay status", "DELAYBUF-ENQ");

  auto delay_status = get_delay_status();
  if (delay_status == DelayStatus::OVER || delay_status == DelayStatus::EXACT) {
    Tools::Log::Warn(
        std::format("Enqueue not viable ({})", delay_status_str(delay_status)),
        "DELAYBUF-ENQ");
    return std::unexpected(TelemetryErr::DELAY_UNDERSHOOT);
  }

  {
    Tools::LoggedScopedLock lock("enqueue() (enq)", _enq_mut);
    size_t idx = _insert_cursor % _frames.size();
    {
      Tools::LoggedScopedLock frame_lock("enqueue() (frame specific mut)",
                                         _frames.at(idx).mut);
      _frames.at(idx).populate(enc_str);
    }

    _insert_cursor++;
  }

  return {};
}

std::expected<void, TelemetryErr>
TelemetryDelayBuffer::dequeue(proto::telemetry::ErpMessage &&in_msg) {
  Tools::Log::Debug("Get delay status", "DELAYBUF-DEQ");
  auto delay_status = get_delay_status();
  if (delay_status == DelayStatus::UNDER ||
      delay_status == DelayStatus::ERROR) {
    Tools::Log::Warn(
        std::format("Dequeue not viable ({})", delay_status_str(delay_status)),
        "DELAYBUF-DEQ");
    return std::unexpected(TelemetryErr::DELAY_OVERSHOOT);
  }

  {
    Tools::LoggedScopedLock deq_lock("dequeue() (deq)", _deq_mut);

    size_t idx = _read_cursor % _frames.size();

    Tools::LoggedScopedLock frame_lock("dequeue() (frame specific mut)",
                                       _frames.at(idx).mut);

    Frame &frame = _frames.at(idx);

    assert(frame.valid);
    // if (!frame.valid)
    //   return std::unexpected(TelemetryErr::FRAME_INVALID);

    frame.swap(std::move(in_msg));
    frame.valid = false;
  }

  _read_cursor++;

  return {};
}

}; // namespace Telemetry
