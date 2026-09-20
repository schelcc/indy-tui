#pragma once

#include "core.hpp"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <variant>

namespace ThreadSafe {

template <typename T, typename Mut = std::shared_mutex,
          typename U = std::remove_cvref_t<T>>
struct LockPair {
private:
  std::variant<std::shared_lock<Mut>, std::unique_lock<Mut>> lock;
  T val;

public:
  template <typename S = std::remove_cv_t<T>>
  LockPair(Core::IsOneOf<std::shared_lock<Mut>, std::unique_lock<Mut>> auto &&l,
           S &&v)
      : lock(std::move(l)), val(std::forward<S>(v)) {}

  // Return a (possibly const) pointer to T
  auto operator->() {
    if constexpr (std::is_same_v<T, U const &>)
      return static_cast<const U *>(&val);
    else
      return static_cast<U *>(&val);
  }

  // Return a (possibly const) ref to T
  auto &&operator*() {
    if constexpr (std::is_same_v<T, U const &>)
      return static_cast<const U &>(val);
    else
      return static_cast<U &>(val);
  }
};

// Don't permit pointer types because pointer semantics don't really make sense
// here
template <typename T>
  requires(!std::is_pointer_v<T>)
class Locked {
  mutable std::shared_mutex _mtx;
  T _val;

public:
  template <typename U = std::remove_cv_t<T>>
  Locked(U &&u) : _val(std::forward<U>(u)) {};

  LockPair<T &> get_mut() { return {std::unique_lock(_mtx), _val}; }
  LockPair<T &> get_mut() const = delete;

  LockPair<T const &> get_const() const {
    return {std::shared_lock(_mtx), _val};
  }
};
}; // namespace ThreadSafe
