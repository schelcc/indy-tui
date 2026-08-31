#include <chrono>
#include <concepts>
#include <memory>
#include <semaphore>
#include <thread>

namespace Tools {

/** @brief Simple timed worker which releases a binary semaphore every period of
 * its configured duration. */
class Timer {
  std::shared_ptr<std::binary_semaphore> _release;
  std::shared_ptr<std::binary_semaphore> _stagger;

  std::binary_semaphore _start{0};

  using duration_t = std::chrono::duration<size_t, std::ratio<1, 1000>>;
  std::atomic<duration_t> _duration;

  std::atomic_bool _running;

  std::thread _thread;

public:
  Timer(std::shared_ptr<std::binary_semaphore> release,
        std::chrono::duration<size_t, std::ratio<1, 1000>> duration =
            std::chrono::milliseconds(1000))
      : _release(release), _duration(duration), _running(true) {
    _thread = std::thread{[this]() {
      // Block for start signal
      _start.acquire();

      while (_running) {
        // TODO : Add pausing
        _release->release();
        std::this_thread::sleep_for(_duration.load());
      }
    }};
  }
  Timer(std::shared_ptr<std::binary_semaphore> release,
        std::shared_ptr<std::binary_semaphore> stagger,
        std::chrono::duration<size_t, std::ratio<1, 1000>> duration =
            std::chrono::milliseconds(1000))
      : _release(release), _stagger(stagger), _duration(duration),
        _running(true) {
    _thread = std::thread{[this]() {
      // Block for start signal
      _start.acquire();

      while (_running) {
        _stagger->acquire();
        _release->release();
        std::this_thread::sleep_for(_duration.load());
      }
    }};
  }

  /** @brief Start the timer. Does nothing if the timer is already started.
   * Thread safe. */
  void start() noexcept { _start.release(); }

  /** @brief Stop the timer. Thread cannot return from stopped state, making the
   * timer useless after stop is called. Thread safe. */
  void stop() noexcept { _running = false; }

  /** @brief Change the timer's period. Thread safe. */
  void set_period(std::convertible_to<duration_t> auto const period) {
    _duration.load(period);
  }
};

}; // namespace Tools
