#pragma once
#include <array>
#include <atomic>
#include <expected>
#include <latch>
#include <memory>
#include <optional>
#include <semaphore>
#include <thread>
#include <vector>

#include "appsync_resolver.hpp"
#include "ixwebsocket/IXWebSocket.h"
#include "ixwebsocket/IXWebSocketMessage.h"
#include "telemetry/driver_telemetry.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_frame.hpp"
#include "telemetry/telemetry_queue.hpp"
#include "time.hpp"

namespace Core {

static constexpr std::array<std::string_view, 3> SessionSourceStrs = {
    "Served Remote", "Served Replay", "Local Replay"};

enum class SessionSource {
  SERVED_REMOTE = 0,
  SERVED_DEBUG = 1,
  LOCAL_REPLAY = 2,
};

class Session {
public:
  struct Err {
    enum Kind {
      SOURCE_INVALID,
      BAD_PAYLOAD,
    } kind;
  };

  enum class Status { NOT_STARTED, INITIATING, STARTED };

private:
  // How long to wait to try to acquire the recieve semaphore
  static constexpr Time::Duration::UIntMilliSec RECV_TIMEOUT{50};

  SessionSource _source;

  // Only used if source is local replay
  std::optional<std::string> _file;

  // Only used if a served session
  std::shared_ptr<ix::WebSocket> _socket;
  std::shared_ptr<Tools::AppSyncSession> _appsync_session;

  // Both only used if a served session
  std::string _recv_payload;
  std::binary_semaphore _recv_sem{0};
  std::mutex _recv_mtx;

  Telemetry::TelemetryQueue _queue;

  // Call notify_all on these to trigger
  std::latch _start_latch{1};
  std::latch _stop_latch{1};

  std::atomic_flag _running{true};

  std::atomic<Status> _status = Status::NOT_STARTED;

  std::atomic<Time::TimePoint> _last_msg_time;
  std::atomic<Time::Duration::DblMilliSec> _msg_recv_period;

  std::vector<std::jthread> _threads{};

  /** @brief Callback for message events during initial setup of session. */
  void init_on_message(const ix::WebSocketMessagePtr &);

  /** @brief Callback for message events from the websocket session. Applies
   * only in served sessions. */
  void on_message(const ix::WebSocketMessagePtr &);

  /** @brief Callback for open events from the websocket session. Applies only
   * in served sessions. */
  void on_open(const ix::WebSocketMessagePtr &);

  void set_callbacks(Status const, std::shared_ptr<ix::WebSocket>);

public:
  Telemetry::TelemetryQueue queue{};

  /** @brief Retrieve the configured delay. */
  std::expected<size_t, Telemetry::TelemetryQueue::Err>
  get_delay_sec() noexcept;

  /** @brief Get the time delta between the most recent frame and the next frame
   * to dequeue. */
  Time::Duration::DblMilliSec get_accrued_delay_ms() noexcept;

  /** @brief Populate the live telemetry status. */
  void draw_telem_status(std::shared_ptr<ncpp::Plane>);

  /** @brief Initiate the session. Returns an expected with the first error
   * encountered, if any. */
  std::expected<void, Err> start_session() noexcept;

  /** @brief End the session. Blocks until all threads have ended. */
  void end_session() noexcept;

  /** @brief Configure the session's delay from live telemetry. */
  void set_delay_sec(size_t const) noexcept;

  /** @brief Retrieve the next relevant frame, if available. */
  std::expected<std::unique_ptr<Telemetry::TelemetryFrame>,
                Telemetry::TelemetryQueue::Err>
  next_frame() noexcept;

  Session(SessionSource source) : _source(source) {}
  Session(SessionSource source, std::string_view const file)
      : _source(source), _file(file) {}
};

}; // namespace Core
