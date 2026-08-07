#include <chrono>
#include <fstream>
#include <optional>
#include <print>
#include <stop_token>
#include <string_view>

#include <ixwebsocket/IXWebSocketServer.h>

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

        // {
        //   std::scoped_lock lock(_send_threads_mtx);
        //   assert(!_send_threads.contains(ip.data()));
        //   _send_threads[ip.data()] = Session(session_file, ip, web_socket);
        // };
        std::shared_ptr<Session> sess =
            std::make_shared<Session>(session_file, ip, web_socket);

        auto sock = web_socket.lock();
        if (!sock)
          return;

        sock->setOnMessageCallback([sess](const ix::WebSocketMessagePtr &msg) {
          // const std::string_view ip = connectionState->getRemoteIp();
          // std::scoped_lock lock(_send_threads_mtx);

          // Handle for a new session being created
          if (msg->type == ix::WebSocketMessageType::Open) {
            // std::println("Got open for IP {}", ip);
            sess->start();
            // _send_threads[ip.data()].start();
            return;
          }

          // Only do further action for a close or error message type
          if (!(msg->type == ix::WebSocketMessageType::Close ||
                msg->type == ix::WebSocketMessageType::Error))
            return;

          // Find the existing session, doing nothing if the session doesn't
          // exist
          // auto existing_session = _send_threads.find(ip.data());
          // if (existing_session == _send_threads.end())
          //   return;

          sess->stop();

          // Only here if we want to kill an existing session
          // existing_session->second.stop();

          // _send_threads.erase(ip.data());
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
    : ip(ip_), socket(socket_), fname(fname_) {}

void ServedReplayWorker::Session::start() {
  std::println("Start replay session for IP {}", ip);
  session = std::jthread{[this](std::stop_token stop) {
    std::ifstream session_stream(fname.data(), std::ios::in);

    std::string line{};
    proto::telemetry::ErpMessage msg{};

    // int64_t last_datetime = 0;
    // std::string last_timeofday = "";
    // Time::TimePoint last_sendtime;

    auto to_duration =
        [](std::string_view const ts) -> Time::Duration::UIntMilliSec {
      // "HH:MM:SS:MS"
      size_t hr = std::stoul(ts.substr(0, 2).data());
      size_t min = std::stoul(ts.substr(3, 5).data());
      size_t sec = std::stoul(ts.substr(6, 8).data());
      size_t msec = std::stoul(ts.substr(9).data());
      return Time::Duration::UIntHour(hr) + Time::Duration::UIntMin(min) +
             Time::Duration::UIntSec(sec) + Time::Duration::UIntMilliSec(msec);
    };

    Time::Duration::UIntMilliSec elapsed_time(0u);
    std::optional<Time::Duration::UIntMilliSec> start_time = {};

    Time::TimePoint loop_time = Time::Clock::now();

    while (!stop.stop_requested() && !socket.expired()) {
      Time::TimePoint now = Time::Clock::now();
      elapsed_time += std::chrono::duration_cast<Time::Duration::UIntMilliSec>(
          now - loop_time);
      loop_time = now;

      if (std::getline(session_stream, line)) {
        msg.ParseFromString(Tools::b64_decode(line));

        auto cur_send = to_duration(msg.heartbeats().Get(0).timeofday());

        if (!start_time.has_value()) {
          start_time
        }

        // if (last_timeofday.empty()) {
        //   last_timeofday = msg.heartbeats().Get(0).timeofday();
        //   last_sendtime = Time::Clock::now();
        //   continue;
        // }

        // std::string cur_timeofday = msg.heartbeats().Get(0).timeofday();

        // // Block until this message is ready to be sent
        // if (last_timeofday != cur_timeofday)
        //   std::this_thread::sleep_until(
        //       last_sendtime +
        //       (to_duration(cur_timeofday) - to_duration(last_timeofday)));

        // last_sendtime = Time::Clock::now();
        // last_timeofday = std::move(cur_timeofday);

        // auto const& hbeat = msg.heartbeats().Get(0);
        // int64_t cur_datetime = hbeat.datetime();

        // if (last_datetime == 0) {
        //   last_datetime = cur_datetime;
        // }

        auto sock = socket.lock();
        sock->send(std::format("\"data\":\"{}\"}}}}}}}}", line));
      }
    }

    session_stream.close();
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
