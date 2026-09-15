#pragma once

#include "ncpp/CellStyle.hh"
#include <cstdint>
#include <memory>
#include <ncpp/CellStyle.hh>
#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>

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

enum class Style : uint16_t {
  NORMAL = 0,
  UNDERLINE = static_cast<uint16_t>(ncpp::CellStyle::Underline),
  BOLD = static_cast<uint16_t>(ncpp::CellStyle::Bold),
  ITALIC = static_cast<uint16_t>(ncpp::CellStyle::Italic),
  STRUCK = static_cast<uint16_t>(ncpp::CellStyle::Struck),
  UNDERCURL = static_cast<uint16_t>(ncpp::CellStyle::Undercurl),
};

static inline uint64_t MakeChannels(std::unique_ptr<ncpp::Plane> &p,
                                    Color const c) {
  uint64_t chan = p->get_channels();

  if (c == Color::NONE)
    return chan;

  ncpp::Palette pal{};

  unsigned int r;
  unsigned int g;
  unsigned int b;

  pal.get(static_cast<int>(c), r, g, b);

  ncchannels_set_fg_rgb8(&chan, r, g, b);

  return chan;
}

static inline uint64_t MakeChannels(std::shared_ptr<ncpp::Plane> p,
                                    Color const c) {
  uint64_t chan = p->get_channels();

  if (c == Color::NONE)
    return chan;

  ncpp::Palette pal{};

  unsigned int r;
  unsigned int g;
  unsigned int b;

  pal.get(static_cast<int>(c), r, g, b);

  ncchannels_set_fg_rgb8(&chan, r, g, b);

  return chan;
}

}; // namespace UI
