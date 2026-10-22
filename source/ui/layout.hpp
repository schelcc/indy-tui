#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include <ncpp/Plane.hh>

#include "core/input.hpp"

#include "tools/locked.hpp"

#include "ui/types.hpp"
#include "ui/views.hpp"

namespace UI {
enum class FocusDirection { NEXT, PREV };
};

namespace Layout {

enum Direction { HORIZONTAL, VERTICAL };

using Segments = uint;

struct Block;

struct BlockList {
  std::vector<Block> elements;

  // This is fine unless we are changing the vec during usage
  ThreadSafe::Locked<std::vector<Block>::iterator> focused_elem_it;

  Direction direction;
  std::shared_ptr<ncpp::Plane> parent;
  Input::InputHandler input_handler;
  std::mutex m;

  std::expected<void, UI::RenderErr>
      solve(std::shared_ptr<ncpp::Plane> = nullptr);

  std::expected<void, UI::RenderErr> render(UI::RenderLevel const, bool const);

  bool take_input(Input::KeyWithMod const &);

  void cycle_focus(UI::FocusDirection const);

  BlockList(Direction, std::vector<Block> &&);

  BlockList(BlockList &&) noexcept;
  BlockList &operator=(BlockList &&) noexcept;

private:
  void init_handler();
};

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
