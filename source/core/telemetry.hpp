#pragma once

#include <atomic>
#include <google/protobuf/descriptor.h>
#include <latch>
#include <memory>
#include <semaphore>
#include <string_view>
#include <thread>

#include "ixwebsocket/IXWebSocket.h"
#include "telemetry_buffer.hpp"

namespace Telemetry {

std::string_view ParsePayload(std::string_view);

/** @brief Interface for sessions. Purely virtual. */
class SessionBase {
public:
  /** @brief Initiate the session, spawning all relevant threads. Thread-safe,
   * non-blocking. */
  virtual void start_session() = 0;

  /** @brief End the configured session, including all threads and timers.
   * Thread-safe, non-blocking. */
  virtual void end_session() = 0;

  /** @brief Retrieve the next telemetry update queued from the session,
   * swapping with an existing telemetry frame. */
  virtual void swap_telemetry(TelemFrame &) noexcept = 0;
};

/** @brief Session type for any kind of served session. Abstract, derived
 * classes must further implement websocket session acquisition. */
class ServedSession : SessionBase {
  std::shared_ptr<TelemetryBuffer> _telem_buf;

  std::latch _start_latch{1};
  std::latch _stop_latch{1};

  std::atomic_bool _session_active{true};
  std::atomic_bool _telem_active{false};

  ix::WebSocket _socket{};
  std::mutex _socket_mut{};

  std::thread _socket_thread{};
  std::thread _enqueue_thread{};

  std::shared_ptr<std::binary_semaphore> _enqueue_rate_limit;

  std::binary_semaphore _enqueue_ready{0};

  std::string _enqueue_string{};
  std::mutex _enqueue_mut{};

  /** @brief Body for socket thread execution. Thread-safe, non-blocking. */
  void socket_thread_worker() noexcept;

  /** @brief Body for enqueue thread execution. Thread-safe, non-blocking. */
  void enqueue_thread_worker() noexcept;

protected:
  // Non-virtual  calls
  /** @brief Block until the socket session has started receiving telemetry. */
  void wait_for_start() noexcept;

  /** @brief Block until the socket session has been closed. */
  void wait_for_stop() noexcept;

  /** @brief Determine if the session is still active. Thread-safe. */
  [[nodiscard]] std::atomic_bool const &session_active() const noexcept;

  /** @brief Determine if the telemetry session is active. Thread-safe. */
  [[nodiscard]] std::atomic_bool const &telem_active() const noexcept;

  /** @brief Activate telemetry session. Releases all threads block by
   * `wait_for_start()`.
   */
  void activate_telem() noexcept;

  /** @brief Store the most recent payload received. Thread-safe, may block. */
  void register_payload(std::string_view);

  /** @brief Callback called when websocket session receives a ping message. */
  void on_ping(const ix::WebSocketMessagePtr &);

  /** @brief Callback called when websocket session receives a pong message. */
  void on_pong(const ix::WebSocketMessagePtr &);

  // Virtual calls
  /** @brief Socket session URL generator. Must be defined by the derived class.
   */
  virtual std::string get_url() const = 0;

  /** @brief Callback called when websocket session is opened. Must be defined
   * by derived class. */
  virtual void on_open(const ix::WebSocketMessagePtr &) = 0;

  /** @brief Callback called when websocket session receives a message. Must be
   * defined by derived class. */
  virtual void on_message(const ix::WebSocketMessagePtr &) = 0;

  /** @brief Callback called when websocket session closes. May be left
   * defaulted. */
  virtual void on_close(const ix::WebSocketMessagePtr &);

  /** @brief Callback called when websocket session receives an error message.
   * May be left defaulted. */
  virtual void on_error(const ix::WebSocketMessagePtr &);

  /** @brief Callback called when websocket session receives a fragment message.
   * May be left defaulted. */
  virtual void on_fragment(const ix::WebSocketMessagePtr &);

public:
  /** @brief Begin the session by connecting to the configured telemetry server
   * and intiating a websocket session. Thread-safe, non-blocking. */
  void start_session() override;

  /** @brief End the session. Thread-safe, blocking. */
  void end_session() override;

  /** @brief TODO */
  void swap_telemetry(TelemFrame &) noexcept override;

  ServedSession(std::shared_ptr<TelemetryBuffer> buf) : _telem_buf(buf) {}
};

class LocalServedReplay : ServedSession {
  std::string_view _host;
  std::string_view _port;

protected:
  /** @brief */
  std::string get_url() const override;

  /** @brief */
  void on_open(const ix::WebSocketMessagePtr &) override;

  /** @brief */
  void on_message(const ix::WebSocketMessagePtr &) override;

public:
  LocalServedReplay(std::shared_ptr<TelemetryBuffer> buf, std::string_view host,
                    std::string_view port)
      : ServedSession(buf), _host(host), _port(port) {}
};

}; // namespace Telemetry
