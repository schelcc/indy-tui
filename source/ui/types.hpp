#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include <ncpp/CellStyle.hh>

namespace UI {

enum class Color {
  BLACK_HARD = 0,
  RED_HARD = 1,
  GREEN_HARD = 2,
  YELLOW_HARD = 3,
  BLUE_HARD = 4,
  PURPLE_HARD = 5,
  CYAN_HARD = 6,
  GRAY_HARD = 7,
  BLACK_SOFT = 8,
  RED_SOFT = 9,
  GREEN_SOFT = 10,
  YELLOW_SOFT = 11,
  BLUE_SOFT = 12,
  PURPLE_SOFT = 13,
  CYAN_SOFT = 14,
  GRAY_SOFT = 15,
  NONE = 16,
};

// enum class FocusState { INIT, FOCUSED, UNFOCUSED };
struct FocusState {
  enum State { INIT, FOCUSED, UNFOCUSED } state;

  [[nodiscard]] operator bool() const noexcept { return state == FOCUSED; }

  [[nodiscard]] bool operator==(FocusState const &o) const noexcept {
    return o.state == state;
  }

  [[nodiscard]] bool operator==(State const &s) const noexcept {
    return state == s;
  }

  FocusState() = default;
  FocusState(State const s) : state(s) {}
  FocusState(bool const b) : state(b ? FOCUSED : UNFOCUSED) {}
};

enum class Align { LEFT, CENTER, RIGHT };

struct BorderCfg {
  bool left;
  bool right;
};

enum class RightBorder { ALLOW, DENY };

enum class Target { FOREGROUND, BACKGROUND };

enum class Style : uint16_t {
  NORMAL = 0,
  UNDERLINE = static_cast<uint16_t>(ncpp::CellStyle::Underline),
  BOLD = static_cast<uint16_t>(ncpp::CellStyle::Bold),
  ITALIC = static_cast<uint16_t>(ncpp::CellStyle::Italic),
  STRUCK = static_cast<uint16_t>(ncpp::CellStyle::Struck),
  UNDERCURL = static_cast<uint16_t>(ncpp::CellStyle::Undercurl),
};

enum class FocusDirection { NEXT, PREV };
using CursorDirection = FocusDirection;

enum class RenderLevel { LIGHT, FULL };

struct RenderErr {
  enum Reason : size_t {
    NULL_PLANE = 0,
    PLANE_OVERFLOW = 1,
    PLANE_UNDERFLOW = 2,
    NOP = 3,
    TABLE_MISMATCH = 4,
    TABLE_ERR = 5,
    STRING_OVERFLOW = 6
  };

  enum Source : size_t { WIDGET = 0, VIEW = 1, LAYOUT = 2, STRING = 3 };

  // Todo : Once reflection is well supported use it here
  static constexpr std::array<std::string_view, 7> REASON_STRS = {
      "NullPlane",     "PlaneOverflow", "PlaneUnderflow", "NoOp",
      "TableMismatch", "TableErr",      "StringOverflow"};

  static constexpr std::array<std::string_view, 4> SOURCE_STRS = {
      "Widget", "View", "Layout", "String"};

  Source source;
  Reason reason;
  std::optional<std::string> msg;
  std::source_location orig;

  [[nodiscard]] std::string what() const noexcept {
    return msg.has_value()
               ? std::format(
                     "error {} from source {} -- {}. Originated from {}:{}.",
                     REASON_STRS[reason].data(), SOURCE_STRS[source].data(),
                     msg.value(), orig.file_name(), orig.line())
               : std::format("error {} from source {}. Originated from {}:{}",
                             REASON_STRS[reason].data(),
                             SOURCE_STRS[source].data(), orig.file_name(),
                             orig.line());
  }

  // Allow implicit conversion of RenderErr to std::expected<T, RenderErr> in
  // order to omit std::unexpected(...)
  template <typename S> operator std::expected<S, RenderErr>() const noexcept {
    return std::unexpected(*this);
  }

  RenderErr(Source s, Reason r,
            std::source_location l = std::source_location::current())
      : source(s), reason(r), msg(std::nullopt), orig(l) {}
  RenderErr(Source s, Reason r, std::string &&m,
            std::source_location l = std::source_location::current())
      : source(s), reason(r), msg(std::move(m)), orig(l) {}
};

}; // namespace UI
