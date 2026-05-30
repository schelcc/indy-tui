#include "core/session.hpp"
#include "app_context.hpp"
#include "appsync_resolver.hpp"
#include "ixwebsocket/IXWebSocket.h"
#include "ixwebsocket/IXWebSocketMessage.h"
#include "telemetry/telemetry_frame.hpp"
#include "telemetry/telemetry_queue.hpp"
#include "time.hpp"
#include <csignal>
#include <optional>
#include <queue>
#include <string_view>
#include <thread>

using Tools::Log;

/** @brief Locate and return the payload contained in the received message, if
 * available. */
std::optional<std::string_view> parse_payload(std::string_view const payload) {
  size_t start_pos = payload.rfind(R"("data":")");
  size_t end_pos = payload.find_last_of('"');

  if ((start_pos == std::string::npos) || (end_pos <= start_pos)) {
    return {};
  }

  // Move to first char of enc string
  start_pos += 8;

  return payload.substr(start_pos, end_pos - start_pos);
}

namespace Core {

Time::Duration::DblMilliSec Session::get_accrued_delay_ms() noexcept {
  return _queue.get_accrued_delay_ms();
}

std::expected<size_t, Telemetry::TelemetryQueue::Err>
Session::get_delay_sec() noexcept {
  return _queue.get_delay_sec();
}

void Session::set_delay_sec(size_t const delay_s) noexcept {
  if (!_queue.set_delay_sec(delay_s).has_value())
    Log::Warn(std::format("Attemped invalid delay setting {}", delay_s));
}

std::expected<std::unique_ptr<Telemetry::TelemetryFrame>,
              Telemetry::TelemetryQueue::Err>
Session::next_frame() noexcept {
  return _queue.dequeue();
}

void Session::set_callbacks(Session::Status const status,
                            std::shared_ptr<ix::WebSocket> socket) {
  if (status == Status::INITIATING && _source != SessionSource::SERVED_DEBUG) {
    Log::Debug("Set callbacks to initiation configuration", "SESS-SOCKET");
    socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
      switch (msg->type) {
        using Type = ix::WebSocketMessageType;
      case Type::Message:
        init_on_message(msg);
        break;
      case Type::Open:
        on_open(msg);
        break;
      case ix::WebSocketMessageType::Close: {
        Log::Info("Session closed by server", "SESS-SOCKET");
        App::AppContext::Shutdown("Websocket session closed by server");
        // kill(0, SIGINT);
        break;
      }
      case ix::WebSocketMessageType::Error: {
        Log::Warn("Unhandled message type 'error' received", "SESS-SOCKET");
        Log::Debug(std::format("DecompressionError: '{}'",
                               msg->errorInfo.decompressionError),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("HTTPStatus: '{}'", msg->errorInfo.http_status),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("Reason: '{}'", msg->errorInfo.reason),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("Retries: '{}'", msg->errorInfo.retries),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("WaitTime: '{}'", msg->errorInfo.wait_time),
                   "SESS-SOCKET-ERR_RECV");
        break;
      }
      case ix::WebSocketMessageType::Ping:
        Log::Warn("Unhandled message type 'ping' received", "SESS-SOCKET");
        break;
      case ix::WebSocketMessageType::Pong:
        Log::Warn("Unhandled message type 'pong' received", "SESS-SOCKET");
        break;
      case ix::WebSocketMessageType::Fragment:
        Log::Warn("Unhandled message type 'fragment' received", "SESS-SOCKET");
        break;
      }
    });
  } else {
    Log::Debug("Set callbacks to started configuration", "SESS-SOCKET");
    socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
      switch (msg->type) {
        using Type = ix::WebSocketMessageType;
      case Type::Message:
        on_message(msg);
        break;
      case Type::Open:
        on_open(msg);
        break;
      case ix::WebSocketMessageType::Close: {
        Log::Info("Session closed by server", "SESS-SOCKET");
        App::AppContext::Shutdown("Websocket session closed by server");
        // kill(0, SIGINT);
        break;
      }
      case ix::WebSocketMessageType::Error: {
        Log::Warn("Unhandled message type 'error' received", "SESS-SOCKET");
        Log::Debug(std::format("DecompressionError: '{}'",
                               msg->errorInfo.decompressionError),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("HTTPStatus: '{}'", msg->errorInfo.http_status),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("Reason: '{}'", msg->errorInfo.reason),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("Retries: '{}'", msg->errorInfo.retries),
                   "SESS-SOCKET-ERR_RECV");
        Log::Debug(std::format("WaitTime: '{}'", msg->errorInfo.wait_time),
                   "SESS-SOCKET-ERR_RECV");
        break;
      }
      case ix::WebSocketMessageType::Ping:
        Log::Warn("Unhandled message type 'ping' received", "SESS-SOCKET");
        break;
      case ix::WebSocketMessageType::Pong:
        Log::Warn("Unhandled message type 'pong' received", "SESS-SOCKET");
        break;
      case ix::WebSocketMessageType::Fragment:
        Log::Warn("Unhandled message type 'fragment' received", "SESS-SOCKET");
        break;
      }
    });
  }
}

std::expected<void, Session::Err> Session::start_session() noexcept {
  std::optional<Session::Err> init_res{};

  // Start the socket session
  // TODO: Work in how local replay works
  _threads.emplace_back(std::jthread{[this, &init_res] {
    _socket = std::make_shared<ix::WebSocket>();

    // Configure the URL
    if (_source == SessionSource::SERVED_REMOTE) {
      _appsync_session =
          std::make_shared<Tools::AppSyncSession>(Tools::resolve_appsync());

      _socket->setUrl(_appsync_session->get_connection_url());
    } else if (_source == SessionSource::SERVED_DEBUG) {
      // TODO: Decide on a way to configure this
      _socket->setUrl("ws://127.0.0.1:8080");
    } else {
      init_res = Err(Err::SOURCE_INVALID);
      return;
    }

    Log::Debug(std::format("WebSocket URL: {}", _socket->getUrl()),
               "SESS-SOCKET");

    _socket->addSubProtocol("graphql-ws");

    set_callbacks(Status::INITIATING, _socket);

    if (_source == SessionSource::SERVED_DEBUG)
      _start_latch.count_down();

    Log::Debug("Starting websocket server", "SESS-SOCKET");
    _socket->start();

    _stop_latch.wait();

    Log::Debug("Stopping websocket server", "SESS-SOCKET");
    _socket->stop();
    _socket->close();
  }});

  // Start the enqueue thread
  // _threads.emplace_back(std::jthread{[this] {
  //   // Wait for the session to start
  //   _start_latch.wait();
  //   Log::Debug("Enqueue thread activated", "SESS-ENQ");

  //   while (_running.load()) {
  //     using QueueErr = Telemetry::TelemetryQueue::Err;

  //     // Don't block forever waiting for the recv semaphore
  //     auto block_until = Time::Clock::now() +
  //     Time::Duration::UIntMilliSec(100);

  //     std::scoped_lock lock(_recv_mtx);

  //     if (!_recv_payload.empty()) {
  //       auto enq_res = _queue.enqueue(_recv_payload);

  //       // DELAY_FULL is ok, we just do nothing
  //       if (!enq_res.has_value() &&
  //           (enq_res.error().kind != QueueErr::DELAY_FULL)) {
  //         Log::Warn("Unhandled enqueue error!", "SESS-ENQ");
  //       }
  //     }

  //     std::this_thread::sleep_until(block_until);
  //   }
  // }});

  if (init_res.has_value())
    return std::unexpected(init_res.value());

  return {};
}

void Session::end_session() noexcept {
  _stop_latch.count_down();

  // Destructing all threads will call .join() on them
  Log::Debug("Joining all threads and waiting for conclusion", "SESS");
  _threads.clear();
}

void Session::on_open(const ix::WebSocketMessagePtr &) {
  Log::Debug("Recv. open message", "SESS-SOCKET");

  if (_source == SessionSource::SERVED_REMOTE) {
    _socket->send(Tools::AppSyncSession::CONN_INIT.data());

    set_callbacks(Status::STARTED, _socket);
  }
};

void Session::init_on_message(const ix::WebSocketMessagePtr &msg) {
  if (msg->str.contains(R"("ka")")) {
    Log::Debug("Received initial keep-alive", "SESS-SOCKET");
    _socket->send(_appsync_session->get_registration_body());
  } else if (msg->str.contains(R"("start_ack")")) {
    Log::Debug("Received start ack, starting now...", "SESS-SOCKET");

    // Change callbacks over to started callbacks
    set_callbacks(Status::STARTED, _socket);

    // Release any threads waiting on session start
    _start_latch.count_down();
  }
};

void Session::on_message(const ix::WebSocketMessagePtr &msg) {
  // If we can lock, do so, otherwise just move on since we prioritize
  // sticking to the delay over full receipt
  // if (_recv_mtx.try_lock()) {
  //   auto payload = parse_payload(msg->str);
  //   if (payload.has_value())
  //     _recv_payload = payload.value();

  //   _recv_mtx.unlock();
  // }
  if (msg->str.empty())
    return;

  auto payload = parse_payload(msg->str);
  if (!payload.has_value())
    Log::Warn("Bad payload received", "SESS-ENQ");
  else {
    auto res = _queue.enqueue(payload.value());

    if (!res.has_value() &&
        (res.error().kind != Telemetry::TelemetryQueue::Err::DELAY_FULL))
      Log::Warn("Unhandled enqueue error!", "SESS-ENQ");
  }
};

}; // namespace Core
