#pragma once
#include "tools/numeric.hpp"
#include <cassert>
#include <cmath>
#include <memory>
#include <ncpp/Plane.hh>
#include <notcurses/notcurses.h>
#include <string_view>

namespace Tools::Draw {

static constexpr std::wstring_view PROGBAR_CHARS = L"▉▊▋▌▍▎▏";

static inline std::wstring prog_bar(double pct, size_t chars) {
  using namespace Tools::Numeric;

  assert(leq(pct, 1.0));

  // Replace all chars up to the incomplete portion with a full block
  double amt = pct * static_cast<double>(chars);
  double rem = amt - std::floor(amt);
  double incomp = static_cast<double>(PROGBAR_CHARS.length()) * rem;

  assert(leq(rem, 1.0));

  size_t complete_idx = static_cast<size_t>(std::floor(amt));
  size_t fractional_idx = static_cast<size_t>(std::floor(incomp));

  assert(leq(complete_idx, chars));
  assert(lt(fractional_idx, PROGBAR_CHARS.length()));

  std::wstring bar = L"";
  for (size_t i = 0; i < chars; i++) {
    if (i < complete_idx)
      bar += L"█";
    else if (i == complete_idx)
      bar += PROGBAR_CHARS.at(fractional_idx);
    else
      bar += L" ";
  };

  return bar;
}

static inline void set_row_bg_rgba(std::shared_ptr<ncpp::Plane> p,
                                   size_t const row, unsigned const r,
                                   unsigned const g, unsigned const b,
                                   unsigned const a = 2) {
  // Alpha's channel is 2 bit, but bg forbids alpha = 3
  assert(a < 3);

  // For some godforsaken reason ncchannel_set_alpha expects the 2 alpha bits to
  // be the two most significant bits, so we have to shift the alpha value up to
  // match
  unsigned alpha = a << 28;

  uint64_t chan = p->get_channels();
  assert(ncchannels_set_bg_rgb8(&chan, r, g, b) == 0);
  assert(ncchannels_set_bg_alpha(&chan, alpha) == 0);

  p->stain(row, 1, 1, p->get_dim_x() - 2, chan, chan, chan, chan);

  // Start at x=1 and stop before dim_x - 1 to avoid borders
  // for (unsigned int x = 1; x < p->get_dim_x() - 1; x++) {
  //   ncpp::Cell cell{};
  //   p->get_at(row, x, &cell);
  //   cell.set_bg_alpha(a);
  //   cell.set_bg_rgb8(r, g, b);
  // }
}

}; // namespace Tools::Draw
