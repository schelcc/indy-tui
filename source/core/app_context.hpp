#pragma once
#include <atomic>
#include <chrono>
#include <format>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <string>
#include <string_view>

namespace App {

/** @brief A static singleton to distribute access to the application context,
 * largely for things like requesting a graceful shutdown. This is an
 * alternative to handling signals. */
class AppContext {

  // Control access to the shutdown latch, always try-lock it -- if trying to
  // lock fails then shutdown is in progress elsewhere and the caller need not
  // do anything
  std::mutex _do_shutdown_mtx;
  std::atomic_flag _can_shut_down{true};
  std::latch _shutdown_latch{1};

  std::mutex _reason_mtx;
  std::optional<std::string_view> _reason;

  const std::string _instance_name{std::format(
      "{:%F_%X}", std::chrono::zoned_time{std::chrono::current_zone(),
                                          std::chrono::system_clock::now()})};

  std::shared_ptr<spdlog::logger> _logger;

  /** @brief Retrieve access to the static AppContext instance. */
  static AppContext &Get() {
    static AppContext app{};
    return app;
  }

  AppContext() {
    try {
      _logger =
          spdlog::basic_logger_mt("logger", "logs/" + _instance_name + ".log");
      _logger->set_pattern("[%H:%M:%S:%e] [%L] [t #%t] %v");
    } catch (const spdlog::spdlog_ex &ex) {
      std::println("Log init failed: {}", ex.what());
      std::terminate();
    }
  }

  ~AppContext() = default;

public:
  // Delete copy + move ctors
  AppContext(const AppContext &) = delete;
  AppContext &operator=(const AppContext &) = delete;
  AppContext(AppContext &&) = delete;
  AppContext &operator=(AppContext &&) = delete;

  /** @brief Retrieve this instance's name (date and time, local TZ). */
  static std::string_view InstanceName() { return Get()._instance_name; }

  /** @brief Retrieve this instance's logger. */
  static spdlog::logger &Log() { return *Get()._logger; }

  /** @brief Block the current thread until shutdown is initiated. */
  static void AwaitShutdown() { Get()._shutdown_latch.wait(); }

  /** @brief Initiate shutdown and provide a reason, releasing any shutdown
   * threads. */
  static void Shutdown(std::string_view const reason) {
    // Only the first call to shutdown can do anything
    if (!Get()._can_shut_down.test())
      return;
    else
      Get()._can_shut_down.clear();

    {
      std::scoped_lock lock(Get()._reason_mtx);
      Get()._reason = reason;
    }

    if (Get()._do_shutdown_mtx.try_lock()) {
      Get()._shutdown_latch.count_down();
      Get()._do_shutdown_mtx.unlock();
    }
  }

  /** @brief Retrieve the reason for a shutdown, if available. Graceful shutdown
   * (shutdown initiated by `Shutdown()`) should always have a provided reason.
   */
  static const std::optional<std::string_view> GetReason() {
    std::scoped_lock lock(Get()._reason_mtx);
    return Get()._reason;
  }
};

}; // namespace App
