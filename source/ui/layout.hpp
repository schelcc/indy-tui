#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include <ncpp/Plane.hh>

namespace Layout {

enum Direction { HORIZONTAL, VERTICAL };

using Segments = size_t;

/** @brief Plane construction wrapper to build a directional layout.
 * Modification of underlying planes incurred internally is thread-safe, but
 * regular usage of the constructed planes fall under the same rules as standard
 * notcurses (single-plane action is largely fine concurrenlty, but multi-plane
 * action like reordering is not). */
template <Direction DIRECTION, Segments TOT_SEGMENTS> struct Container {

  // For holding onto segment count for later resizing
  struct Block {
    Segments segments;
    std::shared_ptr<ncpp::Plane> plane;

    Block(Segments s, std::shared_ptr<ncpp::Plane> p)
        : segments(s), plane(p) {};
  };

  std::mutex mtx{};

  std::shared_ptr<ncpp::Plane> base_plane{};

  std::vector<Block> layout_rows{};

  Segments segments_used{0};

  size_t yoff{0};
  size_t xoff{0};

  std::shared_ptr<ncpp::Plane> add_block(Segments const segments) {
    std::scoped_lock lock(mtx);
    assert(segments_used + segments <= TOT_SEGMENTS);
    segments_used += segments;

    size_t dim = 0;
    size_t target_dim = 0;

    if constexpr (DIRECTION == VERTICAL) {
      target_dim = base_plane->get_dim_y();
    } else {
      target_dim = base_plane->get_dim_x();
    }

    dim = (target_dim * segments) / TOT_SEGMENTS;

    if constexpr (DIRECTION == VERTICAL) {
      layout_rows.emplace_back(
          segments,
          std::make_shared<ncpp::Plane>(base_plane.get(), dim,
                                        base_plane->get_dim_x(), yoff, xoff));
      yoff += dim;
    } else {
      layout_rows.emplace_back(
          segments, std::make_shared<ncpp::Plane>(base_plane.get(),
                                                  base_plane->get_dim_y(), dim,
                                                  yoff, xoff));
      xoff += dim;
    }

    return layout_rows.back().plane;
  }

  Container(std::shared_ptr<ncpp::Plane> base) : base_plane(base) {};
};

}; // namespace Layout
