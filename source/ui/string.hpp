#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ncpp/CellStyle.hh>
#include <ncpp/Plane.hh>

#include "tools/cow.hpp"

#include "core/core.hpp"

#include "ui/types.hpp"
#include "ui/ui.hpp"

template <typename T, typename S>
concept IsNot = !std::is_same_v<T, S>;

[[nodiscard]] static inline std::wstring
str_to_wstr(std::string const &str) noexcept {
  std::wstring wstr(str.size(), L' ');

  std::mbstate_t state = std::mbstate_t();
  auto cstr = str.c_str();
  wstr.resize(std::mbsrtowcs(&wstr[0], &cstr, str.size(), &state));
  return wstr;
}

namespace UI {

struct String {
private:
  struct StyleSet {
    uint16_t style = 0;

    [[nodiscard]] bool has_style(UI::Style s) const {
      return static_cast<uint16_t>(style) & static_cast<uint16_t>(s);
    }

    void clear_style() { style = 0; }

    void clear_style(UI::Style s) { style &= (~static_cast<uint16_t>(s)); }

    void set_style(UI::Style s) { style |= (static_cast<uint16_t>(s)); }

    template <typename... Ts>
      requires(Core::IsOneOf<Ts, UI::Style> && ...)
    StyleSet(Ts... cs) : style((static_cast<uint16_t>(cs) | ...)) {}

    StyleSet() : style(static_cast<uint16_t>(UI::Style::NORMAL)) {};
  };

  struct FmtPair {
    StyleSet style_set;
    UI::Color color;
    size_t start;
    size_t length;

    bool operator==(FmtPair const &o) const noexcept {
      return (start == o.start) && (length == o.length) &&
             (style_set.style == o.style_set.style) && (color == o.color);
    }
  };

  std::wstring str;
  std::vector<FmtPair> fmts{};

public:
  [[nodiscard]] size_t size() const noexcept { return str.size(); }

  template <typename S>
    requires requires(S s) { String(s); }
  String &append(S &&s) {
    if constexpr (std::is_same_v<std::remove_reference_t<S>, String>) {
      auto pre_size = str.size();
      str.append(std::forward_like<S>(s.str));
      std::ranges::transform(
          std::forward_like<S>(s.fmts), std::back_inserter(fmts),
          [pre_size](auto &&v) -> FmtPair {
            auto &&[style, color, start, len] = std::forward<decltype(v)>(v);
            return {style, color, start + pre_size, len};
          });
    } else {
      str.append(std::forward<S>(s));
    }
    return *this;
  }

  // Templating column index to permit both lvalue and rvalue references
  void apply_to_plane(ncpp::Plane *const p, size_t const row, size_t const col,
                      size_t const padding_to_col = 0) const noexcept {

    p->putstr(row, col, str.c_str());

    for (auto const &[style, color, start, length] : fmts) {
      if (color != Color::NONE) {
        auto chan = MakeChannels(p, color);
        p->stain(row, start, 1, length, chan, chan, chan, chan);
      }

      if (style.style != static_cast<uint16_t>(Style::NORMAL))
        p->format(row, start, 1, length, static_cast<uint16_t>(style.style));
    }

    if ((col + str.size()) < padding_to_col)
      p->putstr(
          row, col + str.size(),
          std::wstring(padding_to_col - (col + str.size()), L' ').c_str());
  }

  String(std::string const &s, Color const color,
         Core::IsOneOf<UI::Style> auto... stys)
      : str(str_to_wstr(s)) {
    fmts.emplace_back(StyleSet(stys...), color, 0, str.size());
  }

  String(std::string const &s, Color const color) : str(str_to_wstr(s)) {
    fmts.emplace_back(StyleSet(), color, 0, str.size());
  }

  String(std::string const &s) : str(str_to_wstr(s)), fmts() {}

  template <typename S>
    requires(Core::IsOneOf_ignore_cvref<S, std::wstring>)
  String(S &&s, Color const color, Core::IsOneOf<UI::Style> auto... stys)
      : str(std::forward<S>(s)) {
    fmts.emplace_back(StyleSet(stys...), color, 0, str.size());
  }

  template <typename S>
    requires(Core::IsOneOf_ignore_cvref<S, std::wstring>)
  String(S &&s, Color const color) : str(std::forward<S>(s)) {
    fmts.emplace_back(StyleSet(), color, 0, str.size());
  }

  template <typename S>
    requires(Core::IsOneOf_ignore_cvref<S, std::wstring>)
  String(S &&s) : str(std::forward<S>(s)), fmts() {}

  // Conditionally pick corresponding string type for a given string view
  template <typename SV, typename S = std::conditional_t<
                             std::is_same_v<SV, std::string_view>, std::string,
                             std::wstring>>
    requires(Core::IsOneOf<SV, std::string_view, std::wstring_view>)
  String(SV const &s, Color const color = Color::NONE,
         Core::IsOneOf<Style> auto... stys)
      : String(S(s), color, stys...) {}

  // For anything string or wstring convertible
  template <typename S1, typename S2 = std::conditional_t<
                             std::is_convertible_v<S1, std::string>,
                             std::string, std::wstring>>
    requires((std::is_convertible_v<S1, std::string> ||
              std::is_convertible_v<S1, std::wstring>) &&
             !Core::IsOneOf<S1, std::string, std::wstring>)
  String(S1 &&s, Color const color = Color::NONE,
         Core::IsOneOf<Style> auto... stys)
      : String(S2(std::forward<S1>(s)), color, stys...){};

  // Stream operator fallback for everything else
  template <typename S>
    requires((!std::is_convertible_v<S, std::string> &&
              !std::is_convertible_v<S, std::wstring>) &&
             requires(S s) { std::stringstream() << s; })
  String(S &&s, Color const color = Color::NONE,
         Core::IsOneOf<Style> auto... stys)
      : String((std::stringstream() << std::forward<S>(s)).str(), color,
               stys...){};

  String() = default;

  String(String const &other) = default;

  template <typename S>
    requires(Core::IsOneOf<S, String>)
  String &operator+=(S &&s) noexcept {
    auto pre_size = size();
    str.append(std::forward_like<S>(s.str));
    std::ranges::transform(
        std::forward_like<S>(s.fmts), std::back_inserter(fmts),
        [pre_size](auto &&v) -> FmtPair {
          auto &&[style, color, start, len] = std::forward<decltype(v)>(v);
          return {style, color, start + pre_size, len};
        });

    return *this;
  }

  template <typename S>
    requires(!std::is_same_v<std::remove_cvref_t<S>, String>) &&
            requires(S s) { String(s); } && requires(S s) {
              { s.size() } -> std::same_as<size_t>;
            }
  String &operator+=(S &&other) noexcept {
    return this->append(std::forward<S>(other));
  }

  [[nodiscard]] bool operator==(String const &s) const noexcept {
    // Size is the most likely to fail so allow that to bail first
    return (str == s.str) && (fmts == s.fmts);
  }

  [[nodiscard]] bool operator!=(String const &s) const noexcept {
    // Size is the most likely to fail so allow that to bail first
    return (str != s.str) || (fmts != s.fmts);
  }
};

// Handful of commonly used ctors, just to catch blatant problems fast
static_assert(std::is_constructible_v<UI::String, std::string>);
static_assert(std::is_constructible_v<UI::String, std::string, UI::Color>);
static_assert(
    std::is_constructible_v<UI::String, std::string, UI::Color, UI::Style>);
static_assert(std::is_constructible_v<UI::String, std::string, UI::Color,
                                      UI::Style, UI::Style>);
static_assert(std::is_constructible_v<UI::String, std::wstring>);
static_assert(std::is_constructible_v<UI::String, std::wstring, UI::Color>);
static_assert(
    std::is_constructible_v<UI::String, std::wstring, UI::Color, UI::Style>);
static_assert(std::is_constructible_v<UI::String, std::wstring, UI::Color,
                                      UI::Style, UI::Style>);

template <typename S, typename S1>
  requires(std::is_same_v<S1, String> || std::is_same_v<S, String>)
String operator+(S &&s, S1 &&s1) {
  if constexpr (std::is_same_v<S, String>)
    return String(std::move(s)).append(std::move(s1));
  else
    return String(std::move(s1)).append(std::move(s));
}

using MultiStr = std::variant<std::string, std::wstring, UI::String>;

}; // namespace UI
