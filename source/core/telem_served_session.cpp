#include "core/telemetry.hpp"
#include "ixwebsocket/IXWebSocket.h"
#include "logger.hpp"

#include <format>
#include <semaphore>
#include <string_view>

using Tools::Log;
using Tools::LoggedScopedLock;

namespace Telemetry {

/** @brief Body for socket thread execution. Thread-safe, non-blocking. */
void ServedSession::socket_thread_worker() noexcept {
  // TODO: Figure out where to lock for socket thread-safety
  _socket.setUrl(get_url());
  _socket.addSubProtocol("graphql-ws");

  _socket.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
    switch (msg->type) {
    case ix::WebSocketMessageType::Message:
      this->on_message(msg);
      break;
    case ix::WebSocketMessageType::Open:
      this->on_open(msg);
      break;
    case ix::WebSocketMessageType::Close:
      this->on_close(msg);
      break;
    case ix::WebSocketMessageType::Error:
      this->on_error(msg);
      break;
    case ix::WebSocketMessageType::Ping:
      this->on_ping(msg);
      break;
    case ix::WebSocketMessageType::Pong:
      this->on_pong(msg);
      break;
    case ix::WebSocketMessageType::Fragment:
      this->on_fragment(msg);
      break;
    }
  });

  Log::Info(std::format("Starting websocket worker thread w/ URL '{}'",
                        _socket.getUrl()),
            "SESS-SERVED");

  _socket.start();

  wait_for_stop();

  _socket.close();
}

/** @brief Body for enqueue thread execution. Thread-safe, non-blocking. */
void ServedSession::enqueue_thread_worker() noexcept {
  _enqueue_rate_limit = std::make_shared<std::binary_semaphore>(0);

  _telem_buf->register_sem(_enqueue_rate_limit);

  Log::Info("Starting enqueue worker thread", "SESS-SERVED");

  wait_for_start();

  while (session_active()) {
    // Released every buffer period
    _enqueue_rate_limit->acquire();

    // Released whenever a message is ready (should never really block)
    _enqueue_ready.acquire();

    {
      LoggedScopedLock recv_lock("enqueue_thread_worker (recv)", _enqueue_mut);
      _telem_buf->enqueue(ParsePayload(_enqueue_string));
    }
  }
}

/** @brief Block until the socket session has started receiving telemetry. */
void ServedSession::wait_for_start() noexcept { _start_latch.wait(); }

/** @brief Block until the socket session has been closed. */
void ServedSession::wait_for_stop() noexcept { _stop_latch.wait(); }

/** @brief Determine if the session is still active. Thread-safe. */
std::atomic_bool const &ServedSession::session_active() const noexcept {
  return _session_active;
}

/** @brief Determine if the telemetry session is still active. Thread-safe. */
std::atomic_bool const &ServedSession::telem_active() const noexcept {
  return _telem_active;
}

/** @brief Activate session. Releases all threads blocked by `wait_for_start()`.
 */
void ServedSession::activate_telem() noexcept {
  Log::Info("Noting activation of telemetry session...", "SESS-SERVED");
  _start_latch.count_down();
  _telem_active.store(true);
}

/** @brief Store the most recent payload received. Thread-safe, may block. */
void ServedSession::register_payload(std::string_view payload) {
  LoggedScopedLock recv_lock("register_payload (recv)", _enqueue_mut);
  _enqueue_string = payload;
  _enqueue_ready.release();
}

/** @brief Callback called when websocket session closes. May be left
 * defaulted. */
void ServedSession::on_close(const ix::WebSocketMessagePtr &) {
  Tools::Log::Debug("Received session close");
  // TODO: Handle closure
}

/** @brief Callback called when websocket session receives an error message.
 * May be left defaulted. */
void ServedSession::on_error(const ix::WebSocketMessagePtr &) {
  Tools::Log::Warn("Received error message");
  // TODO: Handle error
}

/** @brief Callback called when websocket session receives a fragment message.
 * May be left defaulted. */
void ServedSession::on_fragment(const ix::WebSocketMessagePtr &) {
  Tools::Log::Debug("Received message fragment");
}

/** @brief Callback called when websocket session receives a ping message. */
void ServedSession::on_ping(const ix::WebSocketMessagePtr &) {
  Tools::Log::Debug("Received ping");
  // TODO: Implement proper ping/pong
}

/** @brief Callback called when websocket session receives a pong message. */
void ServedSession::on_pong(const ix::WebSocketMessagePtr &) {
  Tools::Log::Debug("Received ping");
  // TODO: Implement proper ping/pong
}

/** @brief Begin the session by connecting to the configured telemetry server
 * and intiating a websocket session. Thread-safe, non-blocking. */
void ServedSession::start_session() {
  _session_active.store(true);

  _socket_thread = std::thread{[this]() { this->socket_thread_worker(); }};
  _enqueue_thread = std::thread{[this]() { this->enqueue_thread_worker(); }};
  Tools::Log::Info("Telemetry worker threads dispatched", "SESS-SERVED");
}

/** @brief End the session. Thread-safe, blocking. */
void ServedSession::end_session() {
  Tools::Log::Debug("Ending telemetry worker threads", "SESS-SERVED");
  _session_active.store(false);
  _stop_latch.count_down();

  _enqueue_thread.join();
  _socket_thread.join();
  Tools::Log::Info("Telemetry worker threads ended", "SESS-SERVED");
}

/** @brief TODO */
void ServedSession::swap_telemetry(TelemFrame &frame) noexcept {
  _telem_buf->dequeue(frame);
}

// LOCALLY SERVED REPLAY IMPL
std::string LocalServedReplay::get_url() const {
  return std::format("ws://{}:{}", _host, _port);
}

void LocalServedReplay::on_open(const ix::WebSocketMessagePtr &) {
  Log::Debug("Open msg received, initiating telemetry session...",
             "SESS-SERVED-LOC");
  activate_telem();
}

void LocalServedReplay::on_message(const ix::WebSocketMessagePtr &msg) {
  register_payload(msg->str);
}

}; // namespace Telemetry
