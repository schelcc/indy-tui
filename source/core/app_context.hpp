#pragma once
#include <atomic>
#include <latch>
#include <mutex>
#include <optional>
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

  /** @brief Retrieve access to the static AppContext instance. */
  static AppContext &Get() {
    static AppContext app{};
    return app;
  }

  AppContext() = default;
  ~AppContext() = default;

public:
  // Delete copy + move ctors
  AppContext(const AppContext &) = delete;
  AppContext &operator=(const AppContext &) = delete;
  AppContext(AppContext &&) = delete;
  AppContext &operator=(AppContext &&) = delete;

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
