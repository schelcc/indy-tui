#pragma once

#include <chrono>

namespace Core {

/** @brief Concept requiring that type `T` is any of types `Ts`. */
template <typename T, typename... Ts>
concept IsOneOf = (std::is_same_v<T, Ts> || ...);

/** @brief Helper to construct visitors for variants. */
template <typename... Ts> struct Overload : Ts... {
  using Ts::operator()...;
};

}; // namespace Core
