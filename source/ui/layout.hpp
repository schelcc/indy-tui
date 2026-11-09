#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <vector>

#include <ncpp/Plane.hh>

#include "core/input.hpp"

#include "tools/locked.hpp"

#include "ui/types.hpp"
#include "ui/views.hpp"

namespace Layout {

enum Direction { HORIZONTAL, VERTICAL };

using Segments = uint;

struct Block;

/** @brief Represents a horizontal or vertical collection of Blocks. */
struct BlockList {
  mutable std::mutex m;

  std::vector<Block> elements;
  Input::InputHandler input_handler;

  // This is fine unless we are changing the vec during usage
  ThreadSafe::Locked<std::vector<Block>::iterator> focused_elem_it;

  /// @brief Direction of elements in the BlockList's layout
  Direction direction;

  /// @brief The BlockList's parent plane with which the layout is calculated
  std::shared_ptr<ncpp::Plane> parent;

  /** @brief Solve/re-solve the layout. If a plane pointer is provided it is
   * used as the new parent plane. */
  std::expected<void, UI::RenderErr>
      solve(std::shared_ptr<ncpp::Plane> = nullptr);

  /** @brief Render this plane with the given render level and is-focused
   * status. */
  std::expected<void, UI::RenderErr> render(UI::RenderLevel const,
                                            UI::FocusState const);

  /** @brief Handle the given input. If the BlockList does not have an action
   * for the provided input, it is forwarded to the currently focused element.
   * Returns bool denoting whether the input was eventually acted upon. */
  bool take_input(Input::KeyWithMod const &);

  /** @brief Advance the layout's focused element in the provided direction,
   * wrapping around the layout's ends. */
  void cycle_focus(UI::FocusDirection const);

  BlockList(Direction, std::vector<Block> &&);

  BlockList(BlockList &&) noexcept;
  BlockList &operator=(BlockList &&) noexcept;

private:
  /** @brief Set up the BlockList's input handler. Required as the input_handler
   * cannot be moved and must be rebuilt after any ctor. */
  void init_handler();
};

/** @brief Represents a single section of a BlockList. Holds either a View or
 * another BlockList, allowing nested layouts. */
struct Block {
  std::variant<std::shared_ptr<UI::Views::View>, BlockList> element;
  Segments segments;

  // (actually) TODO - Not all Blocks should be dynamically
  // resizing. There should be a way to note a box which should have
  // some set of fixed row/col sizes. For example, the event info
  // might say it can be 4 rows if there's enough space, otherwise 1
  // row. In that case the renderee will need to have some block state
  // information...

  std::shared_ptr<ncpp::Plane> plane = nullptr;

  Block(Segments const, std::shared_ptr<UI::Views::View>);
  Block(Segments const, BlockList &&);
};

}; // namespace Layout
