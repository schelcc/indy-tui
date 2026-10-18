#include <concepts>
#include <memory>
#include <semaphore>
#include <stop_token>
#include <thread>

namespace Tools {

/** @brief Simple timed worker which releases a binary semaphore every period of
 * its configured duration. */
template <typename D>
  requires requires(D d) {
    std::atomic<D>(d);
    std::this_thread::sleep_for(d);
  }
class Timer {
  std::shared_ptr<std::binary_semaphore> _release;
  std::shared_ptr<std::binary_semaphore> _stagger;

  std::binary_semaphore _start{0};

  std::atomic<D> _duration;

  std::jthread _thread;

public:
  Timer(std::shared_ptr<std::binary_semaphore> release, D const duration)
      : _release(release), _duration(duration) {
    _thread = std::jthread{[this](std::stop_token stop_tok) {
      // Block for start signal
      _start.acquire();

      while (!stop_tok.stop_requested()) {
        // TODO : Add pausing
        _release->release();
        std::this_thread::sleep_for(_duration.load());
      }
    }};
  }
  Timer(std::shared_ptr<std::binary_semaphore> release,
        std::shared_ptr<std::binary_semaphore> stagger, D duration)
      : _release(release), _stagger(stagger), _duration(duration) {
    _thread = std::jthread{[this](std::stop_token stop_tok) {
      // Block for start signal
      _start.acquire();

      while (!stop_tok.stop_requested()) {
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
  void stop() noexcept { _thread.request_stop(); }

  /** @brief Change the timer's period. Thread safe. */
  void set_period(std::convertible_to<D> auto const period) {
    _duration.load(period);
  }
};

}; // namespace Tools
