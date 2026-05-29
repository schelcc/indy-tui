#pragma once
#include <array>
#include <atomic>
#include <optional>
#include <semaphore>
#include <utility>

// thread-safe data structures
namespace ThreadSafe {

template <typename T, size_t Size>
  requires std::is_default_constructible_v<T> && (Size > 2)
class Queue {
  std::atomic_size_t _enq_idx = 0;
  std::atomic_size_t _deq_idx = 0;
  std::atomic_size_t _size = 0;

  std::counting_semaphore<Size - 1> _enq_sem{Size - 1};
  std::counting_semaphore<Size - 1> _deq_sem{0};

  std::array<T, Size> _queue{T{}};

public:
  void enqueue(T &&t) noexcept
    requires std::is_move_assignable_v<T>
  {
    _enq_sem.acquire();
    _size++;
    _queue.at(_enq_idx) = std::move(t);
    _enq_idx.store(++_enq_idx % Size);
    _deq_sem.release();
  }

  [[nodiscard]] T dequeue() noexcept {

    --_size;
    _deq_sem.acquire();
    auto output = std::move(_queue.at(_deq_idx));
    _deq_idx.store(++_deq_idx % Size);
    _enq_sem.release();
    return output;
  }

  [[nodiscard]] std::optional<T> try_dequeue() noexcept {
    std::optional<T> output = {};

    if (_deq_sem.try_acquire()) {
      --_size;
      output = std::move(_queue.at(_deq_idx));
      _deq_idx.store(++_deq_idx % Size);
      _enq_sem.release();
    }

    return output;
  }

  template <typename S, typename V>
  [[nodiscard]] std::optional<T>
  try_dequeue_for(std::chrono::duration<S, V> &&d) noexcept {
    std::optional<T> output{{}};

    if (_deq_sem.try_acquire_for(std::forward<decltype(d)>(d))) {
      --_size;
      output.emplace(std::move(_queue.at(_deq_idx)));
      _deq_idx.store(++_deq_idx % Size);
      _enq_sem.release();
    }

    return output;
  }

  template <typename S>
  [[nodiscard]] std::optional<T>
  try_dequeue_until(std::chrono::time_point<S> &&d) noexcept {
    std::optional<T> output{{}};

    if (_deq_sem.try_acquire_until(std::forward<decltype(d)>(d))) {
      --_size;
      output.emplace(std::move(_queue.at(_deq_idx)));
      _deq_idx.store(++_deq_idx % Size);
      _enq_sem.release();
    }

    return output;
  }

  [[nodiscard]] size_t size() const noexcept { return _size.load(); }

  [[nodiscard]] bool empty() const noexcept { return _size.load() == 0; }
};

}; // namespace ThreadSafe
