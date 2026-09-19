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

  // If T can be dereferenced, return a (possibly const) reference to T
  U const &operator->() const &
    requires(requires(T t) { *t; } && std::is_same_v<T, U const &>)
  {
    return val;
  }

  U &operator->()
    requires(requires(T t) { *t; } && std::is_same_v<T, U &>)
  {
    return val;
  }

  // If T cannot be dereferenced, return a (possibly const) pointer to it
  const U *operator->() const &
    requires(!(requires(T t) { *t; }) && std::is_same_v<T, U const &>)
  {
    return &val;
  }

  U *operator->()
    requires(!(requires(T t) { *t; }) && std::is_same_v<T, U &>)
  {
    return &val;
  }

  // The deref op should return a (possibly const) reference to T
  U &operator*()
    requires(std::is_same_v<T, U &>)
  {
    return val;
  }

  U const &operator*() const &
    requires(std::is_same_v<T, U const &>)
  {
    return val;
  }
};

// Don't permit pointer types because pointer semantics don't really make sense
// here
template <typename T>
  requires(!std::is_pointer_v<T>)
class Locked {
  std::shared_mutex _mtx;
  T _val;

public:
  template <typename U = std::remove_cv_t<T>>
  Locked(U &&u) : _val(std::forward<U>(u)) {};

  LockPair<T &> get_mut() { return {std::unique_lock(_mtx), _val}; }

  LockPair<T const &> get_const() { return {std::shared_lock(_mtx), _val}; }
};
}; // namespace ThreadSafe
