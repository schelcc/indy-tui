#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <type_traits>
#include <vector>

#include <ncpp/Plane.hh>

#include "core.hpp"
#include "input.hpp"
#include "tools/locked.hpp"
#include "ui/widget.hpp"
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
  // ThreadSafe::Locked<std::optional<Block &>> focused_element;
  // std::atomic_int focused_element_idx = 0;
  Direction direction;
  std::shared_ptr<ncpp::Plane> parent;
  std::mutex m;

  void solve(std::shared_ptr<ncpp::Plane> = nullptr);
  void render();
  // void take_input(Input::KeyWithMod const &);

  BlockList(Direction, std::vector<Block> &&);

  BlockList(BlockList &&) noexcept;
  BlockList &operator=(BlockList &&) noexcept;
};

struct Block {
  std::variant<std::shared_ptr<UI::Views::View>, BlockList> element;
  Segments segments;

  std::shared_ptr<ncpp::Plane> plane = nullptr;

  Block(Segments const, std::shared_ptr<UI::Views::View>);
  Block(Segments const, BlockList &&);

  // Block(Block const &);
  // Block &operator=(Block const &);

  // Block(Block &&) noexcept;
  // Block &operator=(Block &&) noexcept;
};

}; // namespace Layout
