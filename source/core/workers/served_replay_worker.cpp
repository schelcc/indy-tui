#include <chrono>
#include <fstream>
#include <functional>
#include <optional>
#include <print>
#include <stop_token>
#include <string>
#include <string_view>

#include <ixwebsocket/IXWebSocketServer.h>
#include <thread>

#include "ErpMessage.pb.h"
#include "base64.hpp"
#include "ixwebsocket/IXWebSocketMessage.h"
#include "ixwebsocket/IXWebSocketMessageType.h"
#include "time.hpp"
#include "workers.hpp"

namespace Workers {

ServedReplayWorker::ServedReplayWorker(std::string_view const filename,
                                       std::string_view const hostname_,
                                       size_t const port_)
    : hostname(hostname_), port(port_), session_file(filename) {}

void ServedReplayWorker::operator()(std::stop_token stop) {
  std::stop_callback stop_cleanup(stop, [this] {
    std::scoped_lock lock(_send_threads_mtx);

    // Stop all sessions, blocking until each is done
    std::for_each(std::begin(_send_threads), std::end(_send_threads),
                  [](auto &p) { std::get<1>(p).stop_and_join(); });

    // Can now kill the server
    _server->stop();
  });

  _server = std::make_unique<ix::WebSocketServer>(port, hostname.data());

  _server->setOnConnectionCallback(
      [this](std::weak_ptr<ix::WebSocket> web_socket,
             std::shared_ptr<ix::ConnectionState> conn_state) {
        const std::string ip = conn_state->getRemoteIp();
        std::println("Connection opened for IP {}", ip);

        std::shared_ptr<Session> sess =
            std::make_shared<Session>(session_file, ip, web_socket);

        auto sock = web_socket.lock();
        if (!sock)
          return;

        sock->setOnMessageCallback([sess](const ix::WebSocketMessagePtr &msg) {
          // Handle for a new session being created
          if (msg->type == ix::WebSocketMessageType::Open) {
            sess->start();
            return;
          }

          // Only do further action for a close or error message type
          if (!(msg->type == ix::WebSocketMessageType::Close ||
                msg->type == ix::WebSocketMessageType::Error))
            return;

          sess->stop();
        });
      });

  auto res = _server->listen();
  std::println("Listening...");
  if (!res.first)
    return;
  _server->disablePerMessageDeflate();
  _server->start();
  _server->wait();
}

ServedReplayWorker::Session::Session(std::string const &fname_,
                                     std::string const &ip_,
                                     std::weak_ptr<ix::WebSocket> socket_)
    : ip(ip_), socket(socket_), fname(fname_), recording_props({}) {}

void ServedReplayWorker::Session::parse_recording_props(std::ifstream &fs) {
  // Fill in defaults
  recording_props["INDY-TUI_RECORDING_KIND"] = "UNTIMED";

  auto oldpos = fs.tellg();
  std::string line;
  while (std::getline(fs, line)) {
    if (!line.contains(':')) {
      fs.seekg(oldpos);
      break;
    }

    auto res = Tools::Strings::SplitAtFirst(line, ':');

    if (res.has_value()) {
      auto [key, value] = res.value();
      recording_props[std::string(key)] = std::string(value);
    }

    oldpos = fs.tellg();
  }
}

void ServedReplayWorker::Session::start() {
  std::println("Start replay session for IP {}", ip);
  session = std::jthread{[this](std::stop_token stop) {
    std::ifstream session_stream(fname.data(), std::ios::in);

    std::string line{};
    proto::telemetry::ErpMessage msg{};

    parse_recording_props(session_stream);
    bool timed_recording =
        (recording_props["INDY-TUI_RECORDING_KIND"] == "TIMED");

    Time::TimePoint start = Time::Clock::now();
    Time::TimePoint block_point = start;

    while (!stop.stop_requested() && !socket.expired()) {
      std::string_view payload;

      if (std::getline(session_stream, line)) {
        if (timed_recording) {
          // Skip any non-payload lines
          if (!line.contains(','))
            continue;

          // TODO: Figure out what to do about errors in replay
          auto [elapsed_parsed, payload_parsed] =
              Tools::Strings::SplitAtFirst(line, ',').value();

          auto [sec_parsed, msec_parsed] =
              Tools::Strings::SplitAtFirst(elapsed_parsed, '.').value();
          size_t sec = std::stoul(std::string(sec_parsed));
          size_t msec = std::stoul(std::string(
              msec_parsed.substr(0, std::min(3ul, msec_parsed.size()))));

          block_point = start + Time::Duration::UIntSec(sec) +
                        Time::Duration::UIntMilliSec(msec);

          payload = payload_parsed;
        } else {
          block_point += Time::Duration::UIntMilliSec(100);
          payload = line;
          start = Time::Clock::now();
        }

        auto sock = socket.lock();
        sock->send(std::format("\"data\":\"{}\"}}}}}}}}", payload));

        std::this_thread::sleep_until(block_point);
      } else {
        std::println("Reached end of recording, closing session.");
        break;
      }
    }

    session_stream.close();

    auto sock = socket.lock();
    sock->close();
  }};
}

void ServedReplayWorker::Session::stop() {
  std::println("Stop replay session for IP {}", ip);
  if (session.has_value())
    session.value().request_stop();
}

void ServedReplayWorker::Session::stop_and_join() {
  std::println("Stop replay session for IP {}", ip);
  if (session.has_value()) {
    session.value().request_stop();
    session.value().join();
  };
}

}; // namespace Workers
