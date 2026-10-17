#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ncpp/CellStyle.hh>
#include <ncpp/Plane.hh>

#include "core/core.hpp"

#include "ui/types.hpp"
#include "ui/ui.hpp"

template <typename T, typename S>
concept IsNot = !std::is_same_v<T, S>;

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

public:
  std::vector<
      std::tuple<std::variant<std::string, std::wstring>, Color, StyleSet>>
      str{};

  [[nodiscard]] size_t size() const noexcept { return size_; }

  template <typename S>
    requires requires(S s) { String(s); }
  String &append(S &&s) {
    if constexpr (std::is_same_v<std::remove_reference_t<S>, String>) {
      if constexpr (std::is_rvalue_reference_v<S>)
        str.insert(std::end(str), std::make_move_iterator(std::begin(s.str)),
                   std::make_move_iterator(std::end(s.str)));
      else
        str.insert(std::end(str), std::begin(s.str), std::end(s.str));
      size_ += s.size_;
    } else {
      str.emplace_back(std::forward<S>(s), Color::NONE, Style::NORMAL);
      size_ += s.size();
    }
    return *this;
  }

  // Templating column index to permit both lvalue and rvalue references
  void apply_to_plane(std::unique_ptr<ncpp::Plane> &p, size_t const row,
                      size_t const col) const {

    size_t cur_col = col;

    std::for_each(
        std::cbegin(str), std::cend(str),
        [&](std::tuple<std::variant<std::string, std::wstring>, Color,
                       StyleSet> const &str_tuple) {
          auto const &str = std::get<0>(str_tuple);
          Color const &color = std::get<1>(str_tuple);
          StyleSet const &style = std::get<2>(str_tuple);

          std::visit(
              [&](auto const &s) {
                size_t ylen = 1;
                size_t xlen = s.length();
                p->putstr(row, cur_col, s.c_str());

                if (color != Color::NONE) {
                  auto chan = MakeChannels(p, color);
                  p->stain(row, cur_col, ylen, xlen, chan, chan, chan, chan);
                }

                if (style.style != static_cast<uint16_t>(Style::NORMAL)) {
                  p->format(
                      row, cur_col, ylen, xlen,
                      static_cast<std::underlying_type_t<Style>>(style.style));
                }

                cur_col += xlen;
              },
              str);
        });
  }
  void apply_to_plane(std::shared_ptr<ncpp::Plane> p, size_t const row,
                      size_t const col, size_t const padding_to = 0) const {

    size_t cur_col = col;

    std::for_each(
        std::cbegin(str), std::cend(str),
        [&](std::tuple<std::variant<std::string, std::wstring>, Color,
                       StyleSet> const &str_tuple) {
          auto const &str = std::get<0>(str_tuple);
          Color const &color = std::get<1>(str_tuple);
          StyleSet const &style = std::get<2>(str_tuple);

          std::visit(
              [&](auto const &s) {
                size_t ylen = 1;
                size_t xlen = s.length();
                p->putstr(row, cur_col, s.c_str());

                if (color != Color::NONE) {
                  auto chan = MakeChannels(p, color);
                  p->stain(row, cur_col, ylen, xlen, chan, chan, chan, chan);
                }

                if (style.style != static_cast<uint16_t>(Style::NORMAL)) {
                  p->format(
                      row, cur_col, ylen, xlen,
                      static_cast<std::underlying_type_t<Style>>(style.style));
                }

                cur_col += xlen;
              },
              str);
        });

    for (; cur_col < padding_to; ++cur_col)
      p->putc(row, cur_col, ' ');
  }

  template <Core::IsOneOf<std::string, std::wstring> S>
  String(S &&s, Color const color, Core::IsOneOf<UI::Style> auto... stys)
      : size_(s.size()),
        str({{std::variant<std::string, std::wstring>(std::in_place_type_t<S>(),
                                                      std::forward<S>(s)),
              color, StyleSet(stys...)}}) {}

  template <Core::IsOneOf<std::string, std::wstring> S>
  String(S &&s, Color const color = UI::Color::NONE)
      : size_(s.size()),
        str({{std::variant<std::string, std::wstring>(std::in_place_type_t<S>(),
                                                      std::forward<S>(s)),
              color, StyleSet()}}) {}

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
    size_ =
        std::get<0>(str.at(0)).visit([](auto const &s) { return s.size(); });
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
    size_ =
        std::get<0>(str.at(0)).visit([](auto const &s) { return s.size(); });
  };

  String() = default;

  String &operator+=(String const &other) noexcept {
    str.insert(std::end(str), std::begin(other.str), std::end(other.str));
    size_ += other.size_;
    return *this;
  }

  String &operator+=(String &&other) noexcept {
    str.insert(std::end(str), std::make_move_iterator(std::begin(other.str)),
               std::make_move_iterator(std::end(other.str)));
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
};

template <typename S, typename S1>
  requires(std::is_same_v<S1, String> || std::is_same_v<S, String>)
String operator+(S &&s, S1 &&s1) {
  if constexpr (std::is_same_v<S, String>)
    return String(std::move(s)).append(std::move(s1));
  else
    return String(std::move(s1)).append(std::move(s));
}

using MultiStr = std::variant<std::string, std::wstring, UI::String>;

struct SimpleTableSketcher {
  std::shared_ptr<ncpp::Plane> plane;
  size_t &row;

  SimpleTableSketcher(std::shared_ptr<ncpp::Plane> p, size_t &r)
      : plane(p), row(r) {};

  template <typename FieldName, typename Field>
    requires(Core::IsOneOf<std::remove_cvref_t<Field>, UI::String, std::string,
                           std::wstring> ||
             requires(Field f) { f.value_or("--"); })
  void operator()(FieldName const field_name, Field const &field) {
    UI::String field_val;

    // If the field is an optional, do value_or on it, otherwise just copy it
    if constexpr (requires { field.value_or("--"); }) {
      field_val = field.value_or("--");
    } else {
      field_val = field;
    }

    UI::String row_str = UI::String{field_name} + ": " + field_val;
    row_str.apply_to_plane(plane, row++, 1);
  }
};

}; // namespace UI
