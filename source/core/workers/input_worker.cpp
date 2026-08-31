#include <atomic>
#include <vector>

#include <ncpp/NotCurses.hh>

#include "core/input.hpp"
#include "core/workers.hpp"

#include "core/app_context.hpp"
#include "core/core.hpp"
#include "core/session.hpp"

enum AllowedModifier {
  NONE,
  SHIFT,
  CTRL,
};

bool has_modifier(const ncinput *ni, const AllowedModifier mod) {
  const bool has_ctrl = ncinput_ctrl_p(ni);
  const bool has_shift = ncinput_shift_p(ni);

  switch (mod) {
  case NONE:
    return (!has_shift && !has_ctrl);
  case SHIFT:
    return has_shift;
  case CTRL:
    return has_ctrl;
  default:
    return false;
  }
}

template <typename... Ts>
  requires(std::is_same_v<Ts, AllowedModifier> && ...)
bool has_modifiers(const ncinput *ni, const Ts... mods) {
  return (has_modifier(ni, mods) && ...);
}

bool key_char_is(const wchar_t key, const ncinput *ni) {
  return ((*ni->utf8 == key) && has_modifier(ni, NONE));
}

template <typename... Ts>
  requires(std::is_same_v<Ts, AllowedModifier> && ...)
bool key_char_is(const wchar_t key, const ncinput *ni, const Ts... mods) {
  return ((*ni->utf8 == key) && (has_modifier(ni, mods) && ...));
}

namespace Workers {

void KeyWorker::operator()(std::stop_token stop_tok) {
  Tools::Log::Debug("Started key worker", "WORKER-INPUT");

  nc.linesigs_disable();

  using Input::KeyWithMod;
  using Input::Modifier;

  Input::InputHandler handler{};

  handler
      .register_callback(
          KeyWithMod('=', Modifier::SHIFT), "Increase the delay by 1s",
          [this]() {
            Tools::Log::Debug("Increase delay requested", "WORKER-INPUT");
            sess.set_delay_sec(++delay);
          })
      .register_callback(
          KeyWithMod('-', Modifier::NONE),
          "Decrease the delay by 1s, only if the delay is positive",
          [this]() {
            Tools::Log::Debug("Decrease delay requested", "WORKER-INPUT");
            sess.set_delay_sec(delay == 0 ? 0 : --delay);
          })
      .register_callback(KeyWithMod('q', Modifier::NONE),
                         "Exit the application",
                         []() {
                           Tools::Log::Debug("Quit requested", "WORKER-INPUT");
                           App::AppContext::Shutdown("Quit requested by user");
                         })
      .duplicate_callback(KeyWithMod('q', Modifier::NONE),
                          KeyWithMod('C', Modifier::CTRL));

  while (!stop_tok.stop_requested()) {
    ncinput in{};

    if (nc.get(&INPUT_TIMEOUT, &in) == 0)
      continue;

    if (in.evtype == ncpp::EvType::Release)
      handler.handle_input(in);
  }
}

}; // namespace Workers
