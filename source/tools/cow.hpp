#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "core.hpp"

namespace Tools {

template <typename T>
  requires(Core::CopySafe<T>)
class CopyOnWrite {
  mutable std::variant<T, std::shared_ptr<T>> _obj;
  mutable bool _sharing;

  using Ptr = std::shared_ptr<T>;

  void unshare() const noexcept {
    assert(_sharing);

    T tmp = *std::move(std::get<Ptr>(_obj));

    _sharing = false;
    _obj = std::move(tmp);
  }

  void share() const noexcept {
    assert(!_sharing);

    std::shared_ptr<T> p = std::make_shared<T>(std::get<T>(std::move(_obj)));
    _obj = p;
    _sharing = true;
  }

public:
  bool is_sharing() const noexcept { return _sharing; }

  T &get_mut() noexcept {
    if (_sharing)
      unshare();

    assert(!_sharing);

    return std::get<T>(_obj);
  }

  T const &get_const() const noexcept {
    return _sharing ? *std::get<std::shared_ptr<T>>(_obj) : std::get<T>(_obj);
  }

  CopyOnWrite()
    requires(std::is_default_constructible_v<T>)
      : _obj(T()), _sharing(false) {
    assert(!_sharing);
  }

  CopyOnWrite(CopyOnWrite const &o) : _sharing(true) {
    if (!o._sharing)
      o.share();

    assert(_sharing);
    assert(o._sharing);

    _obj = o._obj;
  }

  CopyOnWrite &operator=(CopyOnWrite const &o) {
    _sharing = true;

    if (!o._sharing)
      o.share();

    assert(_sharing);
    assert(o._sharing);

    _obj = o._obj;

    return *this;
  }

  template <typename... Args>
    requires(std::is_constructible_v<T, Args...>)
  CopyOnWrite(Args &&...args)
      : _obj(std::in_place_type_t<T>(), std::forward<Args>(args)...),
        _sharing(false) {
    assert(!_sharing);
  }

  template <typename U>
    requires(std::is_same_v<std::remove_cvref_t<U>, T>)
  CopyOnWrite(U &&u)
      : _obj(std::in_place_type_t<T>(), std::forward<U>(u)), _sharing(false) {
    assert(!_sharing);
  }

  ~CopyOnWrite() = default;
};

}; // namespace Tools
