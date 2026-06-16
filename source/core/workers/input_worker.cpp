#include <atomic>
#include <vector>

#include <ncpp/NotCurses.hh>

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

void KeyWorker::operator()() {
  Tools::Log::Debug("Started key worker", "WORKER-INPUT");

  nc.linesigs_disable();

  while (running.test()) {
    ncinput in{};

    if (nc.get(&INPUT_TIMEOUT, &in) == 0)
      continue;

    if (in.evtype == ncpp::EvType::Release) {
      // Release-only events
      if (key_char_is('=', &in, SHIFT)) {
        Tools::Log::Debug("Increase delay requested", "WORKER-INPUT");
        sess.set_delay_sec(++delay);
      } else if (key_char_is('-', &in)) {
        Tools::Log::Debug("Decrease delay requested", "WORKER-INPUT");
        sess.set_delay_sec(delay == 0 ? 0 : --delay);
      } else if (key_char_is('q', &in) || (key_char_is('C', &in, CTRL) &&
                                           !key_char_is('C', &in, SHIFT))) {
        Tools::Log::Debug("Quit requrested", "WORKER-INPUT");
        App::AppContext::Shutdown("Quit requested by user");
      }
    }
  }
}

}; // namespace Workers
