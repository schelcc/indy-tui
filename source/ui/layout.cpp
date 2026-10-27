#include <memory>
#include <mutex>
#include <numeric>
#include <span>
#include <variant>

#include "app_context.hpp"
#include "core.hpp"
#include "core/input.hpp"

#include "ui/layout.hpp"
#include "ui/types.hpp"

namespace Layout {

BlockList::BlockList(Direction d, std::vector<Block> &&blocks)
    : elements(std::move(blocks)), input_handler(), direction(d) {
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
  *focused_elem_it.get_mut() = elements.begin();
  input_handler = Input::InputHandler{};
  init_handler();
  return *this;
}

void BlockList::cycle_focus(UI::FocusDirection const d) {
  App::AppContext::Log().debug(
      "Cycling focus {}", (d == UI::FocusDirection::NEXT) ? "forward" : "back");
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

std::expected<void, UI::RenderErr>
BlockList::solve(std::shared_ptr<ncpp::Plane> p) {
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
      BlockList &l = std::get<BlockList>(b.element);
      TRY_DROP(l.solve((l.parent == nullptr) ? b.plane : nullptr));
    }
    // auto res = b.element.visit(Core::Overload{
    //     [&b](BlockList &l) -> std::expected<void, UI::RenderErr> {
    //       // Not sure why I change what plane is handed to blocklists, but
    //       // that's what I had so I'm leaving it for now.
    //       return l.solve((l.parent == nullptr) ? b.plane : nullptr);
    //     },
    //     []([[maybe_unused]] std::shared_ptr<UI::Views::View> &v)
    //         -> std::expected<void, UI::RenderErr> {
    //       // Will eventually need to do something here, but for now don't.
    //       return {};
    //     },
    // });
  }

  return {};
}

std::expected<void, UI::RenderErr>
BlockList::render(UI::RenderLevel const render_level,
                  UI::FocusState const parent_focused) {
  std::unique_lock lock(m);

  if (parent == nullptr)
    return UI::RenderErr(UI::RenderErr::Source::LAYOUT,
                         UI::RenderErr::Reason::NULL_PLANE,
                         "Tried to render layout with a null parent plane");

  // Render only focused child if it's a light render
  if (render_level == UI::RenderLevel::LIGHT) {
    if (parent_focused != UI::FocusState::FOCUSED)
      return {};

    auto it = *focused_elem_it.get_mut();

    /*
    NOTE - After profiling, it seems the visitor dispatch for these
    two-state variants was a bit much. At least more costly than this
    simple conditional. Might do some fun with a switch and a
    consteval for readability idk.
    */

    if (std::holds_alternative<BlockList>(it->element)) {
      TRY_DROP(std::get<BlockList>(it->element)
                   .render(render_level, UI::FocusState::FOCUSED));
    } else {
      auto &render_cb = std::get<std::shared_ptr<UI::Views::View>>(it->element)
                            ->render_callback;
      if (render_cb.has_value()) {
        TRY_DROP(std::invoke(render_cb.value(), render_level, it->plane.get(),
                             UI::FocusState::FOCUSED));
      } else
        return UI::RenderErr(
            UI::RenderErr::Source::VIEW, UI::RenderErr::NOP,
            "Tried rendering a view with no set render callback");
    }
  }

  auto focused_elem = focused_elem_it.get_const();

  for (auto it = elements.begin(); it < elements.end(); it++) {
    UI::FocusState focus_state = (parent_focused && (it == *focused_elem));

    if (std::holds_alternative<BlockList>(it->element)) {
      TRY_DROP(
          std::get<BlockList>(it->element).render(render_level, focus_state));
    } else {
      auto &render_cb = std::get<std::shared_ptr<UI::Views::View>>(it->element)
                            ->render_callback;
      if (render_cb.has_value()) {
        TRY_DROP((*render_cb)(render_level, it->plane.get(), focus_state));
      } else
        return UI::RenderErr(
            UI::RenderErr::Source::VIEW, UI::RenderErr::NOP,
            "Tried rendering a view with no set render callback");
    }
  }

  return {};
}

bool BlockList::take_input(Input::KeyWithMod const &k) {
  if (input_handler.has_registered(k)) {
    input_handler.handle_input(k);
    return true;
  }

  else {
    // clang-format off
    return (*(*focused_elem_it.get_mut()))
        .element.visit(Core::Overload{
            [&k](std::shared_ptr<UI::Views::View> &v) {
	      return v->take_input(k);
	    },
	    [k](BlockList &l) {
	      return l.take_input(k);
	    }});
    // clang-format on
  }
}

}; // namespace Layout
