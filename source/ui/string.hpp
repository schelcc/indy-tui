#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <numeric>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ncpp/CellStyle.hh>
#include <ncpp/Plane.hh>

#include "core/core.hpp"
#include "ui/ui.hpp"

namespace UI {

struct String {
  std::vector<std::tuple<std::variant<std::string, std::wstring>, Color, Style>>
      str{};

  [[nodiscard]] size_t size() const noexcept {
    return std::accumulate(
        std::cbegin(str), std::cend(str), 0,
        [](size_t &&a, auto const &s) -> size_t {
          return a +
                 std::visit([](auto _s) { return _s.size(); }, std::get<0>(s));
        });
  }

  template <typename S>
    requires requires(S s) { String(s); }
  String &append(S &&s) {
    if constexpr (std::is_same_v<std::remove_reference_t<S>, String>) {
      if constexpr (std::is_rvalue_reference_v<S>)
        str.insert(std::end(str), std::make_move_iterator(std::begin(s.str)),
                   std::make_move_iterator(std::end(s.str)));
      else
        str.insert(std::end(str), std::begin(s.str), std::end(s.str));
    } else {
      str.emplace_back(std::forward<S>(s), Color::NONE, Style::NORMAL);
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
                       Style> const &str_tuple) {
          auto const &str = std::get<0>(str_tuple);
          Color const &color = std::get<1>(str_tuple);
          Style const &style = std::get<2>(str_tuple);

          std::visit(
              [&](auto const &s) {
                size_t ylen = 1;
                size_t xlen = s.length();
                p->putstr(row, cur_col, s.c_str());

                if (color != Color::NONE) {
                  auto chan = MakeChannels(p, color);
                  p->stain(row, cur_col, ylen, xlen, chan, chan, chan, chan);
                }

                if (style != Style::NORMAL) {
                  p->format(row, cur_col, ylen, xlen,
                            static_cast<std::underlying_type_t<Style>>(style));
                }

                cur_col += xlen;
              },
              str);
        });
  }
  void apply_to_plane(std::shared_ptr<ncpp::Plane> p, size_t const row,
                      size_t const col) const {

    size_t cur_col = col;

    std::for_each(
        std::cbegin(str), std::cend(str),
        [&](std::tuple<std::variant<std::string, std::wstring>, Color,
                       Style> const &str_tuple) {
          auto const &str = std::get<0>(str_tuple);
          Color const &color = std::get<1>(str_tuple);
          Style const &style = std::get<2>(str_tuple);

          std::visit(
              [&](auto const &s) {
                size_t ylen = 1;
                size_t xlen = s.length();
                p->putstr(row, cur_col, s.c_str());

                if (color != Color::NONE) {
                  auto chan = MakeChannels(p, color);
                  p->stain(row, cur_col, ylen, xlen, chan, chan, chan, chan);
                }

                if (style != Style::NORMAL) {
                  p->format(row, cur_col, ylen, xlen,
                            static_cast<std::underlying_type_t<Style>>(style));
                }

                cur_col += xlen;
              },
              str);
        });
  }

  String(Core::IsOneOf<std::string, std::wstring> auto const &str,
         Color const color = Color::NONE, Style const style = Style::NORMAL)
      : str({{std::variant<std::string, std::wstring>{
                  std::in_place_type_t<std::remove_cvref_t<decltype(str)>>(),
                  str},
              color, style}}) {};

  String(std::string_view const str, Color const color = Color::NONE,
         Style const style = Style::NORMAL)
      : str({{std::variant<std::string, std::wstring>{
                  std::in_place_type_t<std::string>(), str.data()},
              color, style}}) {};

  String(std::wstring_view const str, Color const color = Color::NONE,
         Style const style = Style::NORMAL)
      : str({{std::variant<std::string, std::wstring>{
                  std::in_place_type_t<std::wstring>(), str.data()},
              color, style}}) {};

  // template <typename S>
  //   requires std::is_same_v<std::remove_cvref_t<S>, Core::MultiStr>
  // String(S &&str)
  //     : str(std::make_tuple(std::forward<S>(str), Color{}, Style{})) {}

  String() = default;

  String &operator+=(String const &other) noexcept {
    str.insert(std::end(str), std::begin(other.str), std::end(other.str));
    return *this;
  }

  String &operator+=(String &&other) noexcept {
    str.insert(std::end(str), std::make_move_iterator(std::begin(other.str)),
               std::make_move_iterator(std::end(other.str)));
    return *this;
  }

  template <typename S>
    requires(!std::is_same_v<std::remove_cvref_t<S>, String>) &&
            requires(S s) { String(s); }
  String &operator+=(S &&other) noexcept {
    return this->append(std::forward<S>(other));
  }
};

template <typename S1, typename S2>
  requires requires(S1 s1, S2 s2) {
    String(s1);
    String(s2);
  }
String operator+(S1 &&s1, S2 &&s2) {
  return String(std::forward<S1>(s1)).append(std::forward<S2>(s2));
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
