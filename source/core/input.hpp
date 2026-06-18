#pragma once

#include "core.hpp"
#include <cstdint>
#include <functional>
#include <notcurses/notcurses.h>
#include <type_traits>

namespace Input {

enum class Modifier : size_t {
  NONE = 0b00,
  SHIFT = 0b01,
  CTRL = 0b10,
  CTRL_SHIFT = 0b11,
};

struct KeyWithMod {
  wchar_t key{};
  Modifier mod{};

  inline bool operator==(const KeyWithMod &other) const {
    return (this->key == other.key) && (this->mod == other.mod);
  }
};

struct KeyCallback {
  std::function<void()> callback{};

  // Track a small description of what keys do for a later "controls help" menu
  // like lazygit's "?" menu
  std::string_view desc{};

  template <typename Func>
    requires std::is_invocable_v<Func>
  KeyCallback(std::string_view const _desc, Func &&_callback)
      : callback(
            [_callback = std::move(_callback)]() { std::invoke(_callback); }),
        desc(_desc) {}
};

}; // namespace Input

namespace std {

// https://www.reddit.com/r/cpp_questions/comments/us3nyb/why_doesnt_c_have_a_default_pairint_int_hash/
template <> struct hash<Input::KeyWithMod> {
  inline size_t operator()(const Input::KeyWithMod &k) const {
    using ModType = std::underlying_type_t<decltype(k.mod)>;
    return std::rotl(std::hash<decltype(k.key)>{}(k.key), 1) ^
           std::hash<ModType>{}(static_cast<ModType>(k.mod));
  }
};

}; // namespace std

namespace Input {

struct InputHandler {
  std::unordered_map<KeyWithMod, KeyCallback> input_table{};

  template <typename Func>
    requires std::is_invocable_v<Func>
  InputHandler &register_callback(KeyWithMod &&k, std::string_view const desc,
                                  Func &&f) {
    input_table.insert_or_assign(std::move(k), KeyCallback(desc, std::move(f)));
    return *this;
  }

  InputHandler &duplicate_callback(KeyWithMod &&src, KeyWithMod &&dst) {
    // TODO: Should src not existing be a problem?
    auto callback = input_table.find(std::move(src));

    if (callback != input_table.end())
      input_table.insert_or_assign(std::move(dst), callback->second);

    return *this;
  }

  void handle_input(const ncinput &ni) {
    const bool has_ctrl = ncinput_ctrl_p(&ni);
    const bool has_shift = ncinput_shift_p(&ni);
    // Can figure out Modifier using 2 bits
    const Modifier mod = Modifier((static_cast<unsigned short>(has_ctrl) << 1) +
                                  (static_cast<unsigned short>(has_shift)));

    auto callback = input_table.find(KeyWithMod(*ni.utf8, mod));

    if (callback == input_table.end())
      return;

    std::invoke(callback->second.callback);
  }
};

}; // namespace Input
