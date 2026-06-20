#pragma once
#include "core.hpp"
#include "tools/numeric.hpp"
#include "ui/layout.hpp"
#include <cassert>
#include <cmath>
#include <memory>
#include <ncpp/CellStyle.hh>
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

enum class Target {
  FG,
  BG,
};

static inline uint64_t mask_pal_channels(std::shared_ptr<ncpp::Plane> p,
                                         Target const target,
                                         PaletteColors const color) {
  uint64_t chan = p->get_channels();

  if (color == PaletteColors::NONE)
    return chan;

  ncpp::Palette pal{};

  unsigned int r;
  unsigned int g;
  unsigned int b;

  pal.get(static_cast<int>(color), r, g, b);

  if (target == Target::FG)
    ncchannels_set_fg_rgb8(&chan, r, g, b);
  else
    ncchannels_set_bg_rgb8(&chan, r, g, b);

  return chan;
}

static inline void set_row_color(std::shared_ptr<ncpp::Plane> p,
                                 size_t const row, Target const target,
                                 PaletteColors const color) {
  uint64_t chan = mask_pal_channels(p, target, color);

  p->stain(row, 1, 1, p->get_dim_x() - 2, chan, chan, chan, chan);
}

static inline std::string trunc_str(std::string_view const s,
                                    size_t const max_len) {
  return s.length() <= (max_len - 3)
             ? std::string{s}
             : std::string{s.substr(0, max_len)} + "...";
}

static inline void
border_with_title(std::shared_ptr<ncpp::Plane> p, std::string const &name,
                  UI::Focus const focus = UI::Focus::INACTIVE) {
  auto color = mask_pal_channels(p, Target::FG,
                                 (focus == UI::Focus::INACTIVE)
                                     ? PaletteColors::NONE
                                     : PaletteColors::BLUE_SOFT);

  p->perimeter_rounded(ncpp::NCBox::CornerMask, color, 0);

  p->putstr(0, 1, name.c_str());
  p->stain(0, 1, 1, name.length(), color, color, color, color);
}

}; // namespace Tools::Draw
