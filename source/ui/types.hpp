#pragma once

#include <cstdint>
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

// using Color = Tools::Draw::PaletteColors;

enum class Target { FOREGROUND, BACKGROUND };

enum class Style : uint16_t {
  NORMAL = 0,
  UNDERLINE = static_cast<uint16_t>(ncpp::CellStyle::Underline),
  BOLD = static_cast<uint16_t>(ncpp::CellStyle::Bold),
  ITALIC = static_cast<uint16_t>(ncpp::CellStyle::Italic),
  STRUCK = static_cast<uint16_t>(ncpp::CellStyle::Struck),
  UNDERCURL = static_cast<uint16_t>(ncpp::CellStyle::Undercurl),
};

}; // namespace UI
