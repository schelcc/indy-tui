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

using MultiStr = std::variant<std::string, std::wstring>;

}; // namespace Core
