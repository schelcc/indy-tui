#pragma once
#include "core.hpp"
#include "tools/numeric.hpp"
#include <cassert>
#include <cmath>
#include <memory>
#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>
#include <notcurses/notcurses.h>
#include <string_view>

namespace Tools::Draw {

static constexpr std::wstring_view PROGBAR_CHARS_FULL_HEIGHT = L"▏▎▍▌▋▊▉";
static constexpr std::wstring_view PROGBAR_CHARS_NARROW = L"╌┄┈";

enum class BarType {
  FULL_HEIGHT,
  SHORT,
};

static inline std::wstring prog_bar(double pct, size_t chars,
                                    BarType bar_type = BarType::FULL_HEIGHT) {
  using namespace Tools::Numeric;

  std::wstring complete_char{};
  std::wstring space_char = L" ";
  std::wstring_view char_str;
  switch (bar_type) {
  case BarType::FULL_HEIGHT: {
    complete_char = L"█";
    char_str = PROGBAR_CHARS_FULL_HEIGHT;
    break;
  };
  case BarType::SHORT: {
    complete_char = L"─";
    space_char = L" ";
    char_str = PROGBAR_CHARS_NARROW;
    break;
  };
  };

  // assert(leq(pct, 1.0));
  pct = std::min(pct, 1.0);

  // Replace all chars up to the incomplete portion with a full block
  double amt = pct * static_cast<double>(chars);
  double rem = std::abs(amt - std::floor(amt));
  double incomp = static_cast<double>(char_str.length()) * rem;

  assert(leq(rem, 1.0));

  size_t complete_idx = static_cast<size_t>(std::floor(amt));
  size_t fractional_idx = static_cast<size_t>(std::floor(incomp));

  assert(leq(complete_idx, chars));
  assert(lt(fractional_idx, char_str.length()));

  std::wstring bar = L"";
  for (size_t i = 0; i < chars; i++) {
    if (i < complete_idx)
      bar += complete_char;
    else if (i == complete_idx)
      bar += char_str.at(fractional_idx);
    else
      bar += space_char;
  };

  return bar;
}

enum class PaletteColors {
  RED_HARD = 0,
  GREEN_HARD = 1,
  YELLOW_HARD = 2,
  BLUE_HARD = 3,
  PURPLE_HARD = 4,
  CYAN_HARD = 5,
  LIGHT_GRAY_HARD = 6,
  DARK_GRAY_HARD = 7,
  GREEN_SOFT = 8,
  YELLOW_SOFT = 9,
  BLUE_SOFT = 10,
  PURPLE_SOFT = 11,
  CYAN_SOFT = 12,
  LIGHT_GRAY_SOFT = 13,
  DARK_GRAY_SOFT = 14,
};

enum class Target {
  FG,
  BG,
};

static inline void set_row_color(std::shared_ptr<ncpp::Plane> p,
                                 size_t const row, Target const target,
                                 PaletteColors const color) {
  uint64_t chan = p->get_channels();

  ncpp::Palette pal{};

  unsigned int r;
  unsigned int g;
  unsigned int b;

  pal.get(static_cast<int>(color), r, g, b);

  if (target == Target::FG)
    ncchannels_set_fg_rgb8(&chan, r, g, b);
  else
    ncchannels_set_bg_rgb8(&chan, r, g, b);

  p->stain(row, 1, 1, p->get_dim_x() - 2, chan, chan, chan, chan);
}

static inline std::string trunc_str(std::string_view const s,
                                    size_t const max_len) {
  return s.length() <= (max_len - 3)
             ? std::string{s}
             : std::string{s.substr(0, max_len)} + "...";
}

}; // namespace Tools::Draw
