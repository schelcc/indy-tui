#include "telemetry/telemetry_queue.hpp"
#include "base64.hpp"
#include "logger.hpp"
#include "telemetry/telemetry_frame.hpp"
#include "time.hpp"
#include <csignal>
#include <cstddef>

using Tools::LoggedSharedLock;
using Tools::LoggedUniqueLock;

using Time::Clock;
using namespace Time::Duration;

namespace Telemetry {

[[nodiscard]] std::expected<void, TelemetryQueue::Err>
TelemetryQueue::set_delay_sec(size_t const delay_sec) noexcept {
  if (delay_sec > MAX_DELAY_S)
    return Err(Err::ILLEGAL_DELAY);
  std::scoped_lock lock(_delay_info_mtx);
  _delay_info.set_delay_s(delay_sec);
  rebuild_delay();
  return {};
}

[[nodiscard]] std::expected<void, TelemetryQueue::Err>
TelemetryQueue::set_refresh_hz(size_t const refresh_hz) noexcept {
  if ((refresh_hz > MAX_REFRESH_HZ) || (refresh_hz < MIN_REFRESH_HZ))
    return Err(Err::ILLEGAL_REFRESH);
  std::scoped_lock lock(_delay_info_mtx);
  _delay_info.set_refresh_hz(refresh_hz);
  rebuild_delay();
  return {};
}

[[nodiscard]] std::expected<size_t, TelemetryQueue::Err>
TelemetryQueue::get_delay_sec() noexcept {
  return _delay_info.get_delay_s();
}

[[nodiscard]] std::expected<size_t, TelemetryQueue::Err>
TelemetryQueue::get_refresh_hz() noexcept {
  return _delay_info.get_refresh_hz();
}

[[nodiscard]] DblMilliSec TelemetryQueue::get_accrued_delay_ms() noexcept {
  assert(_deq_idx <= _enq_idx);

  return static_cast<double>(_enq_idx - _deq_idx) *
         _delay_info.get_frame_period().value_or(
             Time::Duration::DblMilliSec(0));
}

[[nodiscard]] size_t TelemetryQueue::get_total_frames() noexcept {
  return _delay_info.get_delay_frames().value_or(0);
}

void TelemetryQueue::rebuild_delay() noexcept {
  std::unique_lock frame_lock(_frame_mtx);

  std::scoped_lock lock(_delay_info_mtx);

  // Min delay is 2 for now
  size_t prev_delay_frames = _frames.size();
  size_t new_delay_frames = _delay_info.get_delay_frames().value_or(2);

  size_t skip_frames = (new_delay_frames < prev_delay_frames)
                           ? prev_delay_frames - new_delay_frames
                           : 0;

  std::vector<LockedFrame> new_frames(new_delay_frames);

  // Handle start
  if (_enq_idx == 0) {
    _frames = std::move(new_frames);
    return;
  }

  _deq_idx += skip_frames;

  if (prev_delay_frames > 0) {
    for (size_t copy_idx = _deq_idx; copy_idx < _enq_idx; ++copy_idx) {
      new_frames.at(copy_idx % new_delay_frames).frame =
          std::move(_frames.at(copy_idx % prev_delay_frames).frame);
    }
  }

  _frames = std::move(new_frames);
}

std::expected<void, TelemetryQueue::Err>
TelemetryQueue::enqueue(std::string_view const payload) noexcept {
  assert(!payload.empty());
  assert(_deq_idx <= _enq_idx);

  if ((_enq_idx - _deq_idx) < _delay_info.get_delay_frames()) {
    std::shared_lock full_lock(_frame_mtx);

    assert(!_frames.empty());
    auto &cur_frame = _frames.at(_enq_idx % _frames.size());
    std::scoped_lock single_lock(cur_frame.mtx);

    cur_frame.frame =
        std::make_unique<TelemetryFrame>(Tools::b64_decode(payload));

    return cur_frame.frame->is_valid()
               ? std::expected<void, TelemetryQueue::Err>{}
               : std::unexpected(Err{Err::FRAME_INVALID});
  } else {
    return std::unexpected(Err{Err::DELAY_FULL});
  }
}

std::expected<std::unique_ptr<TelemetryFrame>, TelemetryQueue::Err>
TelemetryQueue::dequeue() noexcept {
  assert(_deq_idx <= _enq_idx);
  if ((_enq_idx - _deq_idx) >= _delay_info.get_delay_frames()) {
    std::shared_lock full_lock(_frame_mtx);

    assert(!_frames.empty());
    auto &cur_frame = _frames.at(_deq_idx % _frames.size());
    std::scoped_lock single_lock(cur_frame.mtx);

    return std::move(cur_frame.frame);
  } else {
    return std::unexpected(Err{Err::TOO_RECENT});
  }
}
}; // namespace Telemetry
