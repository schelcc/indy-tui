#include "telemetry/telemetry_queue.hpp"
#include "base64.hpp"
#include "logger.hpp"
#include "telemetry/telemetry_frame.hpp"
#include "time.hpp"
#include <cstddef>

using Tools::LoggedSharedLock;
using Tools::LoggedUniqueLock;

using Time::Clock;
using namespace Time::Duration;

namespace Telemetry {

void TelemetryQueue::set_delay_sec(size_t const delay_sec) noexcept {
  _delay_info.set_delay_s(delay_sec);
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
  std::shared_lock lock(_full_queue_mtx);

  // Front is the oldest, back is the newest
  return _frame_queue.empty()
             ? DblMilliSec(0)
             : TimeBetween<DblMilliSec>(_frame_queue.back().get_modtime(),
                                        _frame_queue.front().get_modtime());
}

std::expected<void, TelemetryQueue::Err>
TelemetryQueue::enqueue(std::string_view const payload) noexcept {
  assert(!payload.empty());

  if ((get_accrued_delay_ms().count() / 1000.0) <=
      static_cast<double>(_delay_info.get_delay_s())) {
    std::unique_lock lock(_full_queue_mtx);

    // We don't have a full delay so we can push a frame
    _frame_queue.push(TelemetryFrame(Tools::b64_decode(payload)));

    // Note to the caller whether the frame was properly parsed
    return _frame_queue.back().is_valid()
               ? std::expected<void, TelemetryQueue::Err>{}
               : std::unexpected(Err{Err::FRAME_INVALID});
  } else {
    return std::unexpected(Err{Err::DELAY_FULL});
  }
}

std::expected<TelemetryFrame, TelemetryQueue::Err>
TelemetryQueue::dequeue() noexcept {
  if ((get_accrued_delay_ms().count() / 1000.0) >=
      static_cast<double>(_delay_info.get_delay_s())) {
    std::unique_lock lock(_full_queue_mtx);
    // We have a fulfilled delay, so we can pull + pop
    auto output = std::move(_frame_queue.front());
    _frame_queue.pop();
    Tools::Log::Debug("Dequeued frame", "QUEUE");
    return output;
  } else {
    return std::unexpected(Err{Err::TOO_RECENT});
  }
}

}; // namespace Telemetry
