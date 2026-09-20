#pragma once

#include <string>
#include <type_traits>
#include <variant>

namespace Core {

/** @brief Concept requiring that type `T` is any of types `Ts`. */
template <typename T, typename... Ts>
concept IsOneOf = (std::is_same_v<T, Ts> || ...);

/** @brief Concept `IsOneOf` but `T` is stripped of any cvref qualifiers. */
template <typename T, typename... Ts>
concept IsOneOf_ignore_cvref = IsOneOf<std::remove_cvref_t<T>, Ts...>;

/** @brief Concept `IsOneOf` but `T` is stripped of any ref qualifiers. */
template <typename T, typename... Ts>
concept IsOneOf_ignore_ref = IsOneOf<std::remove_reference_t<T>, Ts...>;

/** @brief Helper to construct visitors for variants. */
template <typename... Ts> struct Overload : Ts... {
  using Ts::operator()...;
};

/** @brief Requires T be both copy assignable and constructible. */
template <typename T>
concept CopySafe =
    (std::is_copy_assignable_v<T> && std::is_copy_constructible_v<T>);

/** @brief Requires T be both move assignable and constructible. */
template <typename T>
concept MoveSafe =
    (std::is_move_assignable_v<T> && std::is_move_constructible_v<T>);

/** @brief Requires T is a non-const reference */
template <typename T>
concept NotConstRef = std::is_same_v<T, std::remove_cvref_t<T> &>;

/** @brief Requires T is a const reference */
template <typename T>
concept ConstRef = std::is_same_v<T, std::remove_cvref_t<T> const &>;

}; // namespace Core
