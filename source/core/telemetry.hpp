#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <expected>
#include <google/protobuf/descriptor.h>
#include <latch>
#include <string_view>
#include <thread>
#include <vector>

#include "ErpMessage.pb.h"
#include "appsync_resolver.hpp"
#include "base64.hpp"
#include "ixwebsocket/IXWebSocket.h"
#include "tools/logger.hpp"

namespace Telemetry {

struct TelemetryErr {
  enum Kind {
    FRAME_INVALID,
    DELAY_OVERSHOOT,
    DELAY_UNDERSHOOT,
    MISSING_PAYLOAD,
  };

  Kind _kind;
  std::string_view _msg;

  TelemetryErr(Kind kind) : _kind(kind), _msg("") {};
  TelemetryErr(Kind kind, std::string_view msg) : _kind(kind), _msg(msg) {};
};

class TelemetryDelayBuffer {
public:
  enum class DelayStatus { OVER, EXACT, UNDER, ERROR };
  static std::string_view delay_status_str(DelayStatus const &status) {
    switch (status) {
    case DelayStatus::OVER:
      return "OVER";
    case DelayStatus::EXACT:
      return "EXACT";
    case DelayStatus::UNDER:
      return "UNDER";
    case DelayStatus::ERROR:
      return "ERROR";
    }
  }

  /** @brief Set max message rate to `rate` messages per second, recalculate
   * delay frames required.
   */
  void set_max_msg_rate(size_t rate) noexcept {
    _max_msgs_per_sec.store(rate);
    recalculate_delay();
  }

  /** @brief Set delay time to `delay_s` seconds, recalculate delay frames
   * required.
   */
  void set_delay(size_t delay_s) noexcept {
    _delay_s.store(delay_s);
    recalculate_delay();
  }

  [[nodiscard]] size_t get_delay_s() const noexcept { return _delay_s.load(); }

  [[nodiscard]] size_t get_msg_rate() const noexcept {
    return _max_msgs_per_sec.load();
  }

  [[nodiscard]] size_t accrued_delay() noexcept {
    Tools::LoggedScopedLock lock("accrued_delay() (enq, deq)", _enq_mut,
                                 _deq_mut);
    if (_read_cursor > _insert_cursor)
      return 0;
    else
      return (_insert_cursor - _read_cursor) / _max_msgs_per_sec.load();
  }

  [[nodiscard]] size_t accrued_delay_frames() noexcept {
    Tools::LoggedScopedLock lock("accrued_delay_frames() (enq, deq)", _enq_mut,
                                 _deq_mut);
    if (_read_cursor > _insert_cursor)
      return 0;
    else
      return _insert_cursor - _read_cursor;
  }

  [[nodiscard]] std::chrono::time_point<std::chrono::steady_clock>
  get_block_time() const {
    return std::chrono::steady_clock::now() +
           std::chrono::duration(
               std::chrono::milliseconds(1000 / _max_msgs_per_sec.load()));
  }

  [[nodiscard]] DelayStatus get_delay_status();

  std::expected<void, TelemetryErr> enqueue(std::string_view);
  std::expected<void, TelemetryErr> dequeue(proto::telemetry::ErpMessage &&);

  TelemetryDelayBuffer()
      : _max_msgs_per_sec(20), _delay_s(1), _delay_frames(0),
        _frames(std::vector<Frame>()) {
    recalculate_delay();
  }

private:
  /** @brief
   */
  void recalculate_delay();

  std::atomic<size_t> _max_msgs_per_sec;
  std::atomic<size_t> _delay_s;
  std::atomic<size_t> _delay_frames;

  size_t _insert_cursor = 0;
  size_t _read_cursor = 0;

  // These will never block each other, they just block for full access to the
  // frame vec
  std::mutex _deq_mut;
  std::mutex _enq_mut;

  struct Frame {
    bool valid;
    proto::telemetry::ErpMessage msg;
    std::mutex mut;

    // Default to default proto ctor and mark invalid
    Frame() : valid(false), msg(proto::telemetry::ErpMessage{}) {}

    void swap(proto::telemetry::ErpMessage &&in_msg) {
      std::swap(in_msg, msg);
      valid = false;
    }
    void populate(std::string_view enc_str) {
      auto decoded = Tools::b64_decode(enc_str);
      msg.ParseFromString(decoded);
      valid = true;
    };
  };

  std::vector<Frame> _frames;
};

class TelemetrySession {
public:
  void start_session();
  void start_served_replay_session();
  void end_session();

  std::expected<void, TelemetryErr>
  swap_telemetry(proto::telemetry::ErpMessage &&msg) {
    return _delay_queue.dequeue(std::move(msg));
  }

  void set_delay(size_t sec) { _delay_queue.set_delay(sec); }
  void set_refresh(size_t msgs_per_sec) {
    _delay_queue.set_max_msg_rate(msgs_per_sec);
  }

  std::chrono::time_point<std::chrono::steady_clock> get_block_time() {
    return _delay_queue.get_block_time();
  }

  [[nodiscard]] bool ripe() {
    Tools::Log::Debug("Get delay status", "SESS");
    return _delay_queue.get_delay_status() ==
           TelemetryDelayBuffer::DelayStatus::EXACT;
  }

private:
  Tools::AppSyncSession _appsync_session;
  TelemetryDelayBuffer _delay_queue;

  ix::WebSocket _socket;
  bool _started;
  std::atomic_bool _running;
  std::latch _start_latch{1};
  std::latch _finish_latch{1};

  std::mutex _recv_mut;
  std::string _recv_msg;

  // Threads
  std::thread _enqueue_thread;
  void enqueue_thread();

  std::thread _socket_thread;
  void socket_thread();
  void socket_thread_served_replay();

  // Callbacks
  void on_open(const ix::WebSocketMessagePtr &);
  void on_message(const ix::WebSocketMessagePtr &);
  void on_close(const ix::WebSocketMessagePtr &);

  void on_error(const ix::WebSocketMessagePtr &) {};
  void on_ping(const ix::WebSocketMessagePtr &) {};
  void on_pong(const ix::WebSocketMessagePtr &) {};
  void on_fragment(const ix::WebSocketMessagePtr &) {};

  std::expected<std::string_view, TelemetryErr> parse_payload(std::string_view);
};

}; // namespace Telemetry
