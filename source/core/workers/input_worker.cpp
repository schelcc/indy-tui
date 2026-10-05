#include <atomic>
#include <mutex>
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

  while (!stop_tok.stop_requested()) {
    std::unique_lock<std::mutex> lock(key_queue_mtx);
    if (!key_cond.wait(lock, stop_tok, [this] { return !key_queue.empty(); }))
      return;

    root_callback(key_queue.front());
    key_queue.pop();
  }
}

void TestKeyWorker::operator()(std::stop_token stop_tok) {
  Tools::Log::Debug("Started test-key worker", "WORKER-INPUT");

  nc.linesigs_disable();

  using Input::KeyWithMod;
  using Input::Modifier;

  Input::InputHandler handler{};

  while (!stop_tok.stop_requested()) {
    ncinput in{};

    if (nc.get(&INPUT_TIMEOUT, &in) == 0)
      continue;

    if (in.evtype == ncpp::EvType::Release)
      handler.handle_input(in);
  }
}

}; // namespace Workers
