#pragma once

#include <cassert>
#include <cmath>
#include <ncpp/CellStyle.hh>
#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>
#include <notcurses/notcurses.h>
#include <string_view>

#include "app_context.hpp"
#include "tools/numeric.hpp"
#include "ui/types.hpp"

namespace Tools::Draw {

static constexpr std::wstring_view PROGBAR_CHARS_FULL_HEIGHT = L" ▏▎▍▌▋▊▉";
static constexpr std::wstring_view PROGBAR_CHARS_NARROW = L"╌┄┈";

enum class BarType {
  FULL_HEIGHT,
  SHORT,
};

static inline std::wstring
prog_bar(double const in_pct, size_t const chars,
         BarType const bar_type = BarType::FULL_HEIGHT) noexcept {
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

  double pct = std::min(in_pct, 1.0);

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

enum class Target {
  FG,
  BG,
};

static inline uint64_t mask_pal_channels(ncpp::Plane *const p,
                                         Target const target,
                                         UI::Color const color) noexcept {
  uint64_t chan = p->get_channels();

  if (color == UI::Color::NONE)
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

static inline void set_row_color(ncpp::Plane *const p, size_t const row,
                                 Target const target,
                                 UI::Color const color) noexcept {
  uint64_t chan = mask_pal_channels(p, target, color);

  p->stain(row, 1, 1, p->get_dim_x() - 2, chan, chan, chan, chan);
}

static inline std::string trunc_str(std::string_view const s,
                                    size_t const max_len) noexcept {
  return s.length() <= (max_len - 3)
             ? std::string{s}
             : std::string{s.substr(0, max_len)} + "...";
}

static inline void border(ncpp::Plane *const p,
                          UI::Color color = UI::Color::NONE) noexcept {
  auto chan = mask_pal_channels(p, Target::FG, color);

  p->perimeter_rounded(ncpp::NCBox::CornerMask, chan, 0);
}

static inline void
border_with_title(ncpp::Plane *const p, std::string const &name,
                  UI::Color const pal_color = UI::Color::GRAY_HARD) noexcept {
  App::AppContext::Log().debug("Drawing border with title \"{}\"", name);
  auto color = mask_pal_channels(p, Target::FG, pal_color);

  p->perimeter_rounded(ncpp::NCBox::CornerMask, color, 0);

  p->putstr(0, 1, name.c_str());
  p->stain(0, 1, 1, name.length(), color, color, color, color);
}

}; // namespace Tools::Draw
