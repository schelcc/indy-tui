#pragma once

#include <cstdint>
#include <ncpp/CellStyle.hh>
#include <ncpp/Palette.hh>
#include <ncpp/Plane.hh>

#include "ui/types.hpp"

namespace UI {

static inline uint64_t MakeChannels(ncpp::Plane *const p,
                                    Color const c) noexcept {
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
