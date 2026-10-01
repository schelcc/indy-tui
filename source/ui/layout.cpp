#include "ui/layout.hpp"
#include "widget.hpp"
#include <memory>
#include <mutex>
#include <numeric>
#include <span>
#include <variant>

namespace Layout {

BlockList::BlockList(Direction d, std::vector<Block> &&blocks)
    : elements(std::move(blocks)), direction(d) {}

BlockList::BlockList(BlockList &&other) noexcept {
  std::unique_lock lock(other.m);
  elements = std::move(other.elements);
  direction = other.direction;
  parent = other.parent;
  other.parent = nullptr;
}

BlockList &BlockList::operator=(BlockList &&other) noexcept {
  std::unique_lock lock(other.m);
  elements = std::move(other.elements);
  direction = other.direction;
  parent = other.parent;
  other.parent = nullptr;
  return *this;
}

Block::Block(Segments const s, std::shared_ptr<UI::Views::View> w)
    : element(), segments(s) {
  element = w;
}

Block::Block(Segments const s, BlockList &&b)
    : element(std::move(b)), segments(s) {}

void BlockList::solve(std::shared_ptr<ncpp::Plane> p) {
  /*
  For each block, create a new plane with the size corresponding to their
  respective segments and assign it. Afterwards call solve()


  For each block, using the parent plane and the block's segments,
  move and resize the block's plane. If it's a blocklist, call solve on it.
  */
  std::unique_lock lock(m);
  if (p != nullptr)
    parent = p;

  assert(parent != nullptr);

  int x_off = 0;
  int y_off = 0;

  // Small optimization: As long as segments can't update after
  // construct, can move this to the ctor and save the work every
  // resolve
  const uint total_segments =
      std::accumulate(std::begin(elements), std::end(elements), 0u,
                      [](size_t i, Block const &b) { return i + b.segments; });

  uint dim_x = parent->get_dim_x();
  uint dim_y = parent->get_dim_y();

  uint new_block_dim;
  int xlen;
  int ylen;

  for (Block &b : elements) {
    new_block_dim = (((direction == VERTICAL) ? dim_y : dim_x) * b.segments) /
                    total_segments;

    xlen = static_cast<int>((direction == VERTICAL) ? dim_x : new_block_dim);
    ylen = static_cast<int>((direction == VERTICAL) ? new_block_dim : dim_y);

    if (b.plane == nullptr) [[unlikely]]
      b.plane =
          std::make_shared<ncpp::Plane>(parent.get(), ylen, xlen, y_off, x_off);
    else {
      b.plane->resize(0, 0, 0, 0, 0, 0, ylen, xlen);
      b.plane->move(y_off, x_off);
    }

    x_off += (direction == VERTICAL) ? 0 : new_block_dim;
    y_off += (direction == VERTICAL) ? new_block_dim : 0;

    // Solve the children if the block is a blocklist
    if (std::holds_alternative<BlockList>(b.element)) {
      auto &l = std::get<BlockList>(b.element);
      l.solve((l.parent == nullptr) ? b.plane : nullptr);
    }
  }
}

void BlockList::render() {
  std::unique_lock lock(m);

  if (parent == nullptr)
    return;

  // Optimization note: Potentially move rendering out to thread
  // pools? Have leaf blocks be able to dispatch to them? If we are
  // already gonna have the widgets hold on to render callbacks, can't
  // imagine it's too bad.
  for (Block &b : elements) {
    b.element.visit([&b](auto &e) {
      if constexpr (IsLeafNode<decltype(e)>) {
        if (e->render_callback.has_value())
          std::invoke(e->render_callback.value(), b.plane);
      } else
        e.render();
    });
  }
}

}; // namespace Layout
