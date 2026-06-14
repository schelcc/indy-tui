#include <cmath>
#include <limits>
#include <numeric>
#include <type_traits>

namespace Tools::Numeric {

template <typename T> bool lt(T const lhs, T const rhs) {
  if constexpr (!std::is_integral_v<T>)
    return (lhs - rhs) < std::numeric_limits<T>::epsilon();
  else
    return lhs < rhs;
};

template <typename T> bool leq(T const lhs, T const rhs) {
  if constexpr (!std::is_integral_v<T>)
    return (lhs - rhs) <= std::numeric_limits<T>::epsilon();
  else
    return lhs <= rhs;
};

template <typename T> bool gt(T const lhs, T const rhs) {
  if constexpr (!std::is_integral_v<T>)
    return lhs - rhs > std::numeric_limits<T>::epsilon();
  else
    return lhs > rhs;
};

template <typename T> bool geq(T const lhs, T const rhs) {
  if constexpr (!std::is_integral_v<T>)
    return (lhs - rhs) >= std::numeric_limits<T>::epsilon();
  else
    return lhs >= rhs;
};

template <typename T> bool eq(T const lhs, T const rhs) {
  if constexpr (!std::is_integral_v<T>)
    return std::abs(lhs - rhs) < std::numeric_limits<T>::epsilon();
  else
    return lhs == rhs;
};

}; // namespace Tools::Numeric
