#pragma once

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include <ncpp/Plane.hh>

#include "input.hpp"
#include "locked.hpp"
#include "views.hpp"

namespace UI {
enum class FocusDirection { NEXT, PREV };
};

namespace Layout {

enum Direction { HORIZONTAL, VERTICAL };

using Segments = uint;

struct Block;

template <typename T>
concept IsLeafNode =
    std::is_same_v<std::remove_cvref_t<T>, std::shared_ptr<UI::Views::View>>;

struct BlockList {
  std::vector<Block> elements;

  // This is fine unless we are changing the vec during usage
  ThreadSafe::Locked<std::vector<Block>::iterator> focused_elem_it;

  Direction direction;
  std::shared_ptr<ncpp::Plane> parent;
  Input::InputHandler input_handler;
  std::mutex m;

  void solve(std::shared_ptr<ncpp::Plane> = nullptr);
  void render(bool const);
  void take_input(Input::KeyWithMod const &);
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
