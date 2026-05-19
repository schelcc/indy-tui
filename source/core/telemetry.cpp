#include "core/telemetry.hpp"
#include "appsync_resolver.hpp"
#include "tools/logger.hpp"
#include <chrono>
#include <thread>

namespace Telemetry {

void TelemetrySession::start_session() {
  _enqueue_running.store(true);

  auto now = std::chrono::steady_clock::now();
  _last_recv = now;
  _last_enq = now;

  // Dispatch socket, enqueue threads
  _socket_thread = std::thread{[this]() { this->socket_thread(); }};
  _enqueue_thread = std::thread{[this]() { this->enqueue_thread(); }};
  Tools::Log::Info("Telemetry worker threads dispatched", "WS-MAIN");
}

void TelemetrySession::start_served_replay_session() {
  _enqueue_running.store(true);

  _socket_thread =
      std::thread{[this]() { this->socket_thread_served_replay(); }};
  _enqueue_thread = std::thread{[this]() { this->enqueue_thread(); }};
  Tools::Log::Info("Served-replay worker threads dispatched", "WS-MAIN");
}

void TelemetrySession::end_session() {
  _finish.release();
  _stop_enqueue.release();

  _enqueue_thread.join();
  Tools::Log::Info("Stopped enqueue thread", "WS-MAIN");
  _socket_thread.join();
  Tools::Log::Info("Stopped socket thread", "WS-MAIN");
}

void TelemetrySession::socket_thread() {
  Tools::Log::Debug("Starting session", "WS-SOCKET");

  _appsync_session = Tools::resolve_appsync();

  Tools::Log::Debug(std::format("AppSync URL: {}", _appsync_session.uri),
                    "WS-SOCKET");
  Tools::Log::Debug(std::format("AppSync Key: {}", _appsync_session.key),
                    "WS-SOCKET");

  _socket.setUrl(_appsync_session.get_connection_url());
  _socket.addSubProtocol("graphql-ws");

  _started = false;

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

  _socket.start();

  // Block until done latch is opened
  _finish.acquire();

  _socket.close();
}

void TelemetrySession::socket_thread_served_replay() {
  Tools::Log::Debug("Starting served-replay session", "WS-SOCKET");

  _socket.setUrl("ws://127.0.0.1:8080");
  _socket.addSubProtocol("graphql-ws");

  _started = true;

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

  _socket.start();

  // Block until done latch is opened
  _finish.acquire();

  _socket.close();
}

void TelemetrySession::enqueue_thread() {
  Tools::Log::Debug("Blocking for start latch", "WS-ENQ");
  _start.acquire();
  Tools::Log::Debug("Starting worker", "WS-ENQ");

  // Once we *can* acquire the semaphore, we're done
  while (!_stop_enqueue.try_acquire()) {
    if (_recv_sem.try_acquire_until(get_block_time())) {
      Tools::LoggedScopedLock lock("recv_enq, Enqueue worker (recv mut)",
                                   _recv_mut);
      if (!parse_payload(_recv_msg)
               .and_then([this](std::string_view s) {
                 return _delay_queue.enqueue(s);
               })
               .has_value())
        Tools::Log::Warn("Failed to enqueue", "WS-ENQ");
    } else {
      Tools::Log::Debug("Could not acquire recv mut", "WS-ENQ");
    }
    // if (_recv_sem.try_acquire_until(wait_time)) {
    //   Tools::Log::Debug("Acquired recv semaphore", "WS-ENQ");
    //   std::string msg;
    //   {
    //     Tools::LoggedScopedLock lock("recv_enq, Enqueue worker (recv mut)",
    //                                  _recv_mut);
    //     msg = std::move(_recv_msg);
    //   }

    //   auto payload = parse_payload(msg);
    //   if (payload.has_value()) {
    //     auto res [[maybe_unused]] = _delay_queue.enqueue(payload.value());
    //   }

    //   auto now = std::chrono::steady_clock::now();
    //   _enq_period = now - _last_enq.load();
    //   _last_enq = now;

    //   std::this_thread::sleep_until(wait_time);
    // } else {
    //   Tools::Log::Debug("Failed to acquire recv semaphore", "WS-ENQ");
    // }
  }
}

void TelemetrySession::on_open(
    [[maybe_unused]] const ix::WebSocketMessagePtr &msg) {
  Tools::Log::Debug("Opening connection...", "WS-SOCKET");
  if (!_started)
    _socket.send(Tools::AppSyncSession::CONN_INIT.data());
  else {
    Tools::Log::Debug("Skipping AppSync setup");
    _start.release();
  }
}
void TelemetrySession::on_message(const ix::WebSocketMessagePtr &msg) {
  std::string_view msg_str = msg->str;

  if (_started) [[likely]] {
    _recv_msg = msg->str;
    _recv_sem.release();

    auto now = std::chrono::steady_clock::now();
    _recv_period = now - _last_recv.load();
    _last_recv = now;

    if (now > _next_recv_proc.load()) {
      _recv_sem.release();
      _next_recv_proc = get_block_time();
    }

  } else [[unlikely]] {
    if (msg_str.contains("\"ka\"")) {
      Tools::Log::Debug("Received initial keep-alive", "WS-SOCKET");
      _socket.send(_appsync_session.get_registration_body());
    } else if (msg_str.contains("\"start_ack\"")) {
      Tools::Log::Debug("Received start ack, starting now...", "WS");
      auto now = std::chrono::steady_clock::now();

      _last_recv = now;
      _last_enq = now;
      _next_recv_proc = get_block_time();

      _started = true;
      _start.release();
    }
  }
}
void TelemetrySession::on_close(
    [[maybe_unused]] const ix::WebSocketMessagePtr &msg) {
  return;
}

std::expected<std::string_view, TelemetryErr>
TelemetrySession::parse_payload(std::string_view payload) {
  size_t start_pos = payload.rfind(R"("data":")");
  size_t end_pos = payload.find_last_of('"');

  if ((start_pos == std::string::npos) || (end_pos <= start_pos)) {
    return std::unexpected(TelemetryErr(TelemetryErr::MISSING_PAYLOAD));
  }

  // Move to first char of enc string
  start_pos += 8;

  return payload.substr(start_pos, end_pos - start_pos);
}

}; // namespace Telemetry
