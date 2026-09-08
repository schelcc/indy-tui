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

// https://stackoverflow.com/questions/31952237/looking-for-a-constexpr-ceil-function
template <typename T>
  requires requires(T t) {
    static_cast<int>(t);
    t < 0.5;
  }
constexpr int ceil(T const t) {
  const int i = static_cast<int>(t);
  return (t > i) ? i + 1 : i;
}

}; // namespace Tools::Numeric
