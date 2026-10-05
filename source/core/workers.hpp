#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <ixwebsocket/IXWebSocketServer.h>
#include <ncpp/NotCurses.hh>

#include "core/session.hpp"
#include "core/time.hpp"
#include "input.hpp"
#include "layout.hpp"
#include "tools/strings.hpp"

namespace Workers {

struct InterfaceWorker {
  ncpp::NotCurses &nc;
  std::atomic_flag &running;
  Core::Session &sess;

  std::condition_variable_any &key_cond;
  std::queue<Input::KeyWithMod> &key_queue;
  std::mutex &key_queue_mtx;

  /// @brief Maximum interface refresh rate. More than 10 Hz is likely a waste
  /// of resources, and any more than the source telemetry refresh rate is
  /// certainly a waste.
  static constexpr Time::Duration::UIntMilliSec MAX_REDRAW_PERIOD =
      Time::Duration::UIntMilliSec(100);

  void operator()(std::stop_token);
};

struct TestInterfaceWorker {
  ncpp::NotCurses &nc;
  std::atomic_flag &running;

  /// @brief Maximum interface refresh rate. More than 10 Hz is likely a waste
  /// of resources, and any more than the source telemetry refresh rate is
  /// certainly a waste.
  static constexpr Time::Duration::UIntMilliSec MAX_REDRAW_HZ =
      Time::Duration::UIntMilliSec(100);

  void operator()(std::stop_token);
};

struct KeyWorker {
  ncpp::NotCurses &nc;

  std::condition_variable_any &key_cond;
  std::mutex &key_queue_mtx;
  std::queue<Input::KeyWithMod> &key_queue;

  std::atomic_flag &running;
  Core::Session &sess;

  std::move_only_function<void(Input::KeyWithMod const &)> root_callback;

  static constexpr struct timespec INPUT_TIMEOUT{.tv_sec = 5, .tv_nsec = 0};

  void operator()(std::stop_token);
};

struct TestKeyWorker {
  ncpp::NotCurses &nc;
  std::vector<ncpp::NCKey> &key_queue;
  std::atomic_flag &running;

  static constexpr struct timespec INPUT_TIMEOUT{.tv_sec = 5, .tv_nsec = 0};

  void operator()(std::stop_token);
};

struct ServedReplayWorker {
  std::string_view hostname = "127.0.0.1";
  size_t port = 8080;

  std::string session_file = "";

  void operator()(std::stop_token);

  ServedReplayWorker(std::string_view const,
                     std::string_view const = "127.0.0.1", size_t const = 8080);

private:
  struct Session {
    std::optional<std::jthread> session = {};

    std::string ip;
    std::weak_ptr<ix::WebSocket> socket;
    std::string fname;

    std::unordered_map<std::string, std::string> recording_props;

    void start();
    void stop();
    void stop_and_join();

    Session(std::string const &, std::string const &,
            std::weak_ptr<ix::WebSocket>);

    Session() = delete;

  private:
    void parse_recording_props(std::ifstream &);
  };

  std::unique_ptr<ix::WebSocketServer> _server;

  std::mutex _send_threads_mtx;
  std::unordered_map<std::string, Session> _send_threads;

public:
  ServedReplayWorker() = default;
};

}; // namespace Workers
