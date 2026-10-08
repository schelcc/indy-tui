#include "ui/layout.hpp"
#include "input.hpp"
#include "widget.hpp"
#include <memory>
#include <mutex>
#include <numeric>
#include <span>
#include <variant>

namespace Layout {

BlockList::BlockList(Direction d, std::vector<Block> &&blocks)
    : elements(std::move(blocks)), direction(d), input_handler() {
  *focused_elem_it.get_mut() = elements.begin();
  init_handler();
}

void BlockList::init_handler() {
  // Setup input handler
  using namespace Input;

  if (direction == HORIZONTAL) {
    input_handler
        .register_callback(KeyWithMod('H', Modifier::SHIFT), "Cycle focus left",
                           [this] { cycle_focus(UI::FocusDirection::PREV); })
        .register_callback(KeyWithMod('L', Modifier::SHIFT),
                           "Cycle focus right",
                           [this] { cycle_focus(UI::FocusDirection::NEXT); });
  } else {
    input_handler
        .register_callback(KeyWithMod('J', Modifier::SHIFT), "Cycle focus down",
                           [this] { cycle_focus(UI::FocusDirection::NEXT); })
        .register_callback(KeyWithMod('K', Modifier::SHIFT), "Cycle focus up",
                           [this] { cycle_focus(UI::FocusDirection::PREV); });
  }
}

BlockList::BlockList(BlockList &&other) noexcept {
  std::unique_lock lock(other.m);
  elements = std::move(other.elements);
  direction = other.direction;
  parent = other.parent;
  other.parent = nullptr;
  // input_handler = std::move(other.input_handler);
  *focused_elem_it.get_mut() = elements.begin();
  input_handler = Input::InputHandler{};
  init_handler();
}

BlockList &BlockList::operator=(BlockList &&other) noexcept {
  std::unique_lock lock(other.m);
  elements = std::move(other.elements);
  direction = other.direction;
  parent = other.parent;
  other.parent = nullptr;
  // input_handler = std::move(other.input_handler);
  *focused_elem_it.get_mut() = elements.begin();
  input_handler = Input::InputHandler{};
  init_handler();
  return *this;
}

void BlockList::cycle_focus(UI::FocusDirection const d) {
  auto it = focused_elem_it.get_mut();
  if (d == UI::FocusDirection::NEXT)
    *it = ((*it + 1) < elements.end()) ? (*it + 1) : (elements.begin());
  else
    *it = ((*it - 1) >= elements.begin()) ? (*it - 1) : (elements.end() - 1);
}

Block::Block(Segments const s, std::shared_ptr<UI::Views::View> w)
    : element(w), segments(s) {}

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
    // Note: If the renderee needs to have some block state information, this
    // might be a chance to pass it in, in which case this should be a visit.
    // Case for async?
    if (std::holds_alternative<BlockList>(b.element)) {
      auto &l = std::get<BlockList>(b.element);
      l.solve((l.parent == nullptr) ? b.plane : nullptr);
    }
  }
}

void BlockList::render(bool const parent_focused) {
  struct Vis {
    Block &b;
    bool focused;
    void operator()(std::shared_ptr<UI::Views::View> &v) {
      if (v->render_callback.has_value())
        v->render_callback.value()(b.plane, focused);
    }

    void operator()(BlockList &l) { l.render(focused); }
  };
  std::unique_lock lock(m);

  if (parent == nullptr)
    return;

  // Optimization note: Potentially move rendering out to thread
  // pools? Have leaf blocks be able to dispatch to them? If we are
  // already gonna have the widgets hold on to render callbacks, can't
  // imagine it's too bad.
  auto focused_elem = focused_elem_it.get_const();
  for (auto it = elements.begin(); it < elements.end(); it++)
    it->element.visit(Vis{*it, (parent_focused && (it == *focused_elem))});
}

bool BlockList::take_input(Input::KeyWithMod const &k) {
  struct Vis {
    Input::KeyWithMod const &k;
    bool operator()(std::shared_ptr<UI::Views::View> &v) {
      return v->take_input(k);
    }
    bool operator()(BlockList &b) { return b.take_input(k); }
  };

  if (input_handler.has_registered(k)) {
    input_handler.handle_input(k);
    return true;
  }

  else // if (focused_element_idx.get_const()->has_value())
  {
    return (*(*focused_elem_it.get_mut())).element.visit(Vis{k});
    // elements.at(focused_element_idx.get_const()->value_or(0) %
    // elements.size())
    //     .element.visit(Vis{k});
  }
}

}; // namespace Layout
