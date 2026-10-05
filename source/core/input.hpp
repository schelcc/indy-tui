#pragma once

#include "core.hpp"
#include "locked.hpp"
#include "logger.hpp"
#include "time.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <notcurses/notcurses.h>
#include <optional>
#include <type_traits>
#include <unordered_map>

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
  Time::TimePoint event_time;

  KeyWithMod(wchar_t k, Modifier m)
      : key(k), mod(m), event_time(Time::Clock::now()) {};

  KeyWithMod(ncinput const &ni)
      : key(*ni.utf8), event_time(Time::Clock::now()) {
    const bool has_ctrl = ncinput_ctrl_p(&ni);
    const bool has_shift = ncinput_shift_p(&ni);
    // Can figure out Modifier using 2 bits
    mod = Modifier((static_cast<unsigned short>(has_ctrl) << 1) +
                   (static_cast<unsigned short>(has_shift)));
  }

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

  // KeyCallback(KeyCallback &&) = delete;
  // KeyCallback &operator=(KeyCallback &&) = delete;
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
  std::unordered_map<
      KeyWithMod,
      std::variant<KeyCallback, std::reference_wrapper<KeyCallback>>>
      input_table{};

  template <typename Func>
    requires std::is_invocable_v<Func>
  InputHandler &register_callback(KeyWithMod &&k, std::string_view const desc,
                                  Func &&f) {
    input_table.emplace(std::move(k), KeyCallback(desc, std::move(f)));

    return *this;
  }

  InputHandler &duplicate_callback(KeyWithMod &&src, KeyWithMod &&dst) {
    // Future (possible) optimization: Hold the callbacks in a vec, then map
    // keys to indices so that callback duplication is just two mappings which
    // point to the same index. Downside is how removal works, deregistration is
    // gonna always be O(n) then. But I don't think I literally ever deregister
    // so maybe that's a fine sacrifice.

    struct Vis {
      std::variant<KeyCallback, std::reference_wrapper<KeyCallback>>
      operator()(KeyCallback &c) {
        return std::ref(c);
      }

      std::variant<KeyCallback, std::reference_wrapper<KeyCallback>>
      operator()(std::reference_wrapper<KeyCallback> &c) {
        return c;
      }
    };
    // TODO: Should src not existing be a problem?
    auto callback = input_table.find(std::move(src));

    if (callback == input_table.end())
      return *this;

    else if (input_table.contains(dst))
      input_table.at(std::move(dst)) = callback->second.visit(Vis{});
    else
      input_table.emplace(std::move(dst), callback->second.visit(Vis{}));

    return *this;
  }

  void handle_input(KeyWithMod const &k) {
    struct Vis {
      void operator()(KeyCallback &c) { c.callback(); }
      void operator()(std::reference_wrapper<KeyCallback> c) {
        c.get().callback();
      }
    };

    auto callback = input_table.find(k);

    if (callback == input_table.end())
      return;

    callback->second.visit(Vis{});
    Time::Duration::DblMilliSec handle_delay =
        Time::Clock::now() - k.event_time;
    Tools::Log::Debug(std::format("Input '{}' handled in {:.3f}ns", char(k.key),
                                  handle_delay.count() * 1000),
                      "INPUT-TIMING");
  }

  [[nodiscard]] bool has_registered(ncinput const &ni) const {
    return input_table.contains(KeyWithMod(ni));
  }

  [[nodiscard]] bool has_registered(KeyWithMod const &k) const {
    return input_table.contains(k);
  }

  InputHandler() = default;
};

}; // namespace Input
