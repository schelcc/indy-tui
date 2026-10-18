#include <ncpp/Plane.hh>
#include <optional>
#include <string_view>

#include "ixwebsocket/IXWebSocket.h"
#include "ixwebsocket/IXWebSocketMessage.h"

#include "core/app_context.hpp"
#include "core/session.hpp"
#include "core/time.hpp"

#include "tools/appsync_resolver.hpp"

#include "telemetry/telemetry_frame.hpp"
#include "telemetry/telemetry_queue.hpp"

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

Time::Duration::DblMilliSec Session::get_accrued_delay_ms() const noexcept {
  return _queue.get_accrued_delay_ms();
}

std::expected<size_t, Telemetry::TelemetryQueue::Err>
Session::get_delay_sec() const noexcept {
  return _queue.get_delay_sec();
}

void Session::set_delay_sec(size_t const delay_s) noexcept {
  if (!_queue.set_delay_sec(delay_s).has_value())
    App::AppContext::Log().warn("Attemped invalid delay setting {}", delay_s);
}

std::expected<std::unique_ptr<Telemetry::TelemetryFrame>,
              Telemetry::TelemetryQueue::Err>
Session::next_frame() noexcept {
  return _queue.dequeue();
}

void Session::set_callbacks(Session::Status const new_status,
                            std::shared_ptr<ix::WebSocket> socket) {
  status = new_status;
  if (status == Status::INITIATING && _source != SessionSource::SERVED_DEBUG) {
    App::AppContext::Log().debug("Set callbacks to initiation configuration");
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
        App::AppContext::Log().info("Session closed by server");
        App::AppContext::Shutdown("Websocket session closed by server");
        // kill(0, SIGINT);
        break;
      }
      case ix::WebSocketMessageType::Error: {
        App::AppContext::Log().debug("Unhandled message type 'error' received");

        auto &e = msg->errorInfo;

        App::AppContext::Log().debug("(WS Err) DecompressionError: '{}'",
                                     e.decompressionError);
        App::AppContext::Log().debug("(WS Err) HTTPStatus: '{}'",
                                     e.http_status);
        App::AppContext::Log().debug("(WS Err) Reason: '{}'", e.reason);
        App::AppContext::Log().debug("(WS Err) Retries: '{}'", e.retries);
        App::AppContext::Log().debug("(WS Err) WaitTime: '{}'", e.wait_time);
        break;
      }
      case ix::WebSocketMessageType::Ping:
        App::AppContext::Log().warn("Unhandled message type 'ping' received");
        break;
      case ix::WebSocketMessageType::Pong:
        App::AppContext::Log().warn("Unhandled message type 'pong' received");
        break;
      case ix::WebSocketMessageType::Fragment:
        App::AppContext::Log().warn(
            "Unhandled message type 'fragment' received");
        break;
      }
    });
  } else {
    App::AppContext::Log().debug("Set callbacks to started configuration");
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
        App::AppContext::Log().info("Session closed by server");
        App::AppContext::Shutdown("Websocket session closed by server");
        break;
      }
      case ix::WebSocketMessageType::Error: {
        App::AppContext::Log().debug("Unhandled message type 'error' received");

        auto &e = msg->errorInfo;

        App::AppContext::Log().debug("(WS Err) DecompressionError: '{}'",
                                     e.decompressionError);
        App::AppContext::Log().debug("(WS Err) HTTPStatus: '{}'",
                                     e.http_status);
        App::AppContext::Log().debug("(WS Err) Reason: '{}'", e.reason);
        App::AppContext::Log().debug("(WS Err) Retries: '{}'", e.retries);
        App::AppContext::Log().debug("(WS Err) WaitTime: '{}'", e.wait_time);
        break;
      }
      case ix::WebSocketMessageType::Ping:
        App::AppContext::Log().warn("Unhandled message type 'ping' received");
        break;
      case ix::WebSocketMessageType::Pong:
        App::AppContext::Log().warn("Unhandled message type 'pong' received");
        break;
      case ix::WebSocketMessageType::Fragment:
        App::AppContext::Log().warn(
            "Unhandled message type 'fragment' received");
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

    App::AppContext::Log().debug("WebSocket URL: {}", _socket->getUrl());

    _socket->addSubProtocol("graphql-ws");

    set_callbacks(Status::INITIATING, _socket);

    if (_source == SessionSource::SERVED_DEBUG)
      _start_latch.count_down();

    App::AppContext::Log().debug("Starting websocket server");
    _socket->start();

    _stop_latch.wait();

    App::AppContext::Log().debug("Stopping websocket server");
    _socket->stop();
    _socket->close();
  }});

  if (init_res.has_value())
    return std::unexpected(init_res.value());

  return {};
}

void Session::end_session() noexcept {
  _stop_latch.count_down();

  // Destructing all threads will call .join() on them
  App::AppContext::Log().debug(
      "Joining all threads and waiting for conclusion");
  _threads.clear();
}

void Session::on_open(const ix::WebSocketMessagePtr &) {
  App::AppContext::Log().debug("Recv. open message");

  if (_source == SessionSource::SERVED_REMOTE) {
    _socket->send(Tools::AppSyncSession::CONN_INIT.data());
  }
};

void Session::init_on_message(const ix::WebSocketMessagePtr &msg) {
  if (msg->str.contains(R"("ka")")) {
    App::AppContext::Log().debug("Received initial keep-alive");
    _socket->send(_appsync_session->get_registration_body());
  } else if (msg->str.contains(R"("start_ack")")) {
    App::AppContext::Log().debug("Received start ack, starting now...");

    // Set the first message recv timestamp
    last_msg_time = Time::Clock::now();

    // Change callbacks over to started callbacks
    set_callbacks(Status::STARTED, _socket);

    // Release any threads waiting on session start
    _start_latch.count_down();
  }
};

void Session::on_message(const ix::WebSocketMessagePtr &msg) {
  if (msg->str.empty())
    return;

  // Calculate message recv period
  auto now = Time::Clock::now();
  msg_recv_period = now - last_msg_time.load();
  last_msg_time = now;

  auto payload = parse_payload(msg->str);
  if (!payload.has_value())
    App::AppContext::Log().warn("Bad payload received");
  else {
    auto res = _queue.enqueue(payload.value());

    if (!res.has_value() &&
        (res.error().kind != Telemetry::TelemetryQueue::Err::DELAY_FULL))
      App::AppContext::Log().warn("Unhandled enqueue error");
  }
};

}; // namespace Core
