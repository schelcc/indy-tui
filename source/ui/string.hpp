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
  size_t size_ = 0;

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

  struct Triple {
    std::wstring wstr;
    Color color;
    StyleSet style_set;
  };

  static_assert(std::is_nothrow_move_constructible_v<Triple> &&
                std::is_nothrow_move_assignable_v<Triple>);

  // mutable std::variant<std::vector<Triple>,
  //                      std::shared_ptr<std::vector<Triple>>>
  //     str{};

  Tools::CopyOnWrite<std::vector<Triple>> str{};

  static_assert(std::is_nothrow_move_constructible_v<decltype(str)> &&
                std::is_nothrow_move_assignable_v<decltype(str)>);

public:
  // std::vector<
  //     std::tuple<std::variant<std::string, std::wstring>, Color, StyleSet>>
  //     str{};
  // std::vector<Triple> str{};

  [[nodiscard]] size_t size() const noexcept { return size_; }

  template <typename S>
    requires requires(S s) { String(s); }
  String &append(S &&s) {
    if constexpr (std::is_same_v<std::remove_reference_t<S>, String>) {
      if constexpr (std::is_rvalue_reference_v<S>) {
        str.get_mut().append_range(std::move(s.str.get_mut()));
        // str.get_mut().insert(
        //     std::end(str.get_const()),
        //     std::make_move_iterator(std::begin(s.str.get_mut())),
        //     std::make_move_iterator(std::end(s.str.get_mut())));
      } else {
        str.get_mut().append_range(s.str.get_const());
        // str.get_mut().insert(std::end(str.get_mut()),
        //                      std::begin(s.str.get_const()),
        //                      std::end(s.str.get_const()));
      }
      size_ += s.size_;
    } else {
      str.get_mut().emplace_back(std::forward<S>(s), Color::NONE,
                                 Style::NORMAL);
      size_ += s.size();
    }
    return *this;
  }

  // Templating column index to permit both lvalue and rvalue references
  void apply_to_plane(ncpp::Plane *const p, size_t const row, size_t const col,
                      size_t const padding_to_col = 0) const {
    size_t cur_col = col;

    for (auto &[s, color, style] : str.get_const()) {
      size_t ylen = 1;
      size_t xlen = s.length();
      p->putstr(row, cur_col, s.c_str());

      // if (std::holds_alternative<std::string>(str_var)) {
      //   auto &s = std::get<std::string>(str_var);
      //   xlen = s.length();
      //   p->putstr(row, cur_col, s.c_str());
      // } else {
      //   auto &s = std::get<std::wstring>(str_var);
      //   xlen = s.length();
      //   p->putstr(row, cur_col, s.c_str());
      // }

      if (color != Color::NONE) {
        auto chan = MakeChannels(p, color);
        p->stain(row, cur_col, ylen, xlen, chan, chan, chan, chan);
      }

      if (style.style != static_cast<uint16_t>(Style::NORMAL))
        p->format(row, cur_col, ylen, xlen, static_cast<uint16_t>(style.style));

      cur_col += xlen;
    }

    for (; cur_col < padding_to_col; ++cur_col)
      p->putc(row, cur_col, ' ');
  }

  String(std::string const &s, Color const color,
         Core::IsOneOf<UI::Style> auto... stys)
      : size_(s.size()),
        str(std::vector<Triple>{{str_to_wstr(s), color, StyleSet(stys...)}}) {}

  String(std::string const &s, Color const color = UI::Color::NONE)
      : size_(s.size()),
        str(std::vector<Triple>{{str_to_wstr(s), color, StyleSet{}}}) {}

  template <typename S>
    requires(Core::IsOneOf_ignore_cvref<S, std::wstring>)
  String(S &&s, Color const color, Core::IsOneOf<UI::Style> auto... stys)
      : size_(s.size()), str(std::vector<Triple>{
                             {std::forward<S>(s), color, StyleSet(stys...)}}) {}

  template <typename S>
    requires(Core::IsOneOf_ignore_cvref<S, std::wstring>)
  String(S &&s, Color const color = UI::Color::NONE)
      : size_(s.size()),
        str(std::vector<Triple>{{std::forward<S>(s), color, StyleSet{}}}) {}

  // template <Core::IsOneOf<std::string, std::wstring> S>
  // String(S &&s, Color const color, Core::IsOneOf<UI::Style> auto... stys)
  //     : size_(s.size()),
  //       str(std::vector<Triple>{
  //           {std::variant<std::string,
  //           std::wstring>(std::in_place_type_t<S>(),
  //                                                    std::forward<S>(s)),
  //            color, StyleSet(stys...)}}) {};
  // str(std::in_place_type_t<std::vector<Triple>>(),
  //     {{std::variant<std::string, std::wstring>(std::in_place_type_t<S>(),
  //                                               std::forward<S>(s)),
  //       color, StyleSet(stys...)}}) {}

  // template <Core::IsOneOf<std::string, std::wstring> S>
  // String(S &&s, Color const color = UI::Color::NONE)
  //     : size_(s.size()),
  //       str(std::vector<Triple>{
  //           {std::variant<std::string,
  //           std::wstring>(std::in_place_type_t<S>(),
  //                                                    std::forward<S>(s)),
  //            color, StyleSet{}}}) {};

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
      : String(S2(std::forward<S1>(s)), color, stys...) {
    size_ = str.get_const().at(0).wstr.size();
    // if (is_sharing())
    //   size_ = std::get<std::vector<Triple>>(str).at(0).str_var.visit(
    //       [](auto const &s) { return s.size(); });
    // else
    //   size_ = std::get<std::shared_ptr<std::vector<Triple>>>(str)
    //               ->at(0)
    //               .str_var.visit([](auto const &s) { return s.size(); });
  };

  // Stream operator fallback for everything else
  template <typename S>
    requires((!std::is_convertible_v<S, std::string> &&
              !std::is_convertible_v<S, std::wstring>) &&
             requires(S s) { std::stringstream() << s; })
  String(S &&s, Color const color = Color::NONE,
         Core::IsOneOf<Style> auto... stys)
      : String((std::stringstream() << std::forward<S>(s)).str(), color,
               stys...) {
    size_ = str.get_const().at(0).wstr.size();
    // size_ = str.get_const().at(0).str_var.visit(
    //     [](auto const &s) { return s.size(); });
  };

  String() = default;

  String(String const &other) = default;

  String &operator+=(String const &other) noexcept {
    str.get_mut().insert(std::end(str.get_mut()),
                         std::begin(other.str.get_const()),
                         std::end(other.str.get_const()));
    size_ += other.size_;
    return *this;
  }

  String &operator+=(String &&other) noexcept {
    str.get_mut().insert(
        std::end(str.get_mut()),
        std::make_move_iterator(std::begin(other.str.get_mut())),
        std::make_move_iterator(std::end(other.str.get_mut())));

    size_ += other.size_;
    return *this;
  }

  template <typename S>
    requires(!std::is_same_v<std::remove_cvref_t<S>, String>) &&
            requires(S s) { String(s); } && requires(S s) {
              { s.size() } -> std::same_as<size_t>;
            }
  String &operator+=(S &&other) noexcept {
    size_ += other.size();
    return this->append(std::forward<S>(other));
  }

  [[nodiscard]] bool operator==(String const &s) const noexcept {
    // Size is the most likely to fail so allow that to bail first
    return (size_ == s.size_) &&
           std::ranges::equal(str.get_const(), s.str.get_const(),
                              [](auto const &v1, auto const &v2) {
                                auto [svar_1, color_1, style_1] = v1;
                                auto [svar_2, color_2, style_2] = v2;
                                return (color_1 == color_2) &&
                                       (style_1.style == style_2.style) &&
                                       (svar_1 == svar_2);
                              });
  }

  [[nodiscard]] bool operator!=(String const &s) const noexcept {
    // Size is the most likely to fail so allow that to bail first
    return (size_ != s.size_) &&
           !std::ranges::equal(str.get_const(), s.str.get_const(),
                               [](auto const &v1, auto const &v2) {
                                 auto [svar_1, color_1, style_1] = v1;
                                 auto [svar_2, color_2, style_2] = v2;
                                 return (color_1 == color_2) &&
                                        (style_1.style == style_2.style) &&
                                        (svar_1 == svar_2);
                               });
  }
};

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
