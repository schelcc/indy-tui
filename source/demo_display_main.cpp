#include "tools/logger.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>
#include <print>
#include <string_view>
#include <thread>

static constexpr bool BLOCKING = true;
static constexpr bool NON_BLOCKING = true;

int main() {
  setlocale(LC_ALL, "");

  notcurses_options nc_opts{};

  nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_DRAIN_INPUT;

  ncpp::NotCurses nc(nc_opts);
  std::shared_ptr<ncpp::Plane> splane(nc.get_stdplane());

  Tools::Log::SetLevel(Tools::Log::NONE);

  ncinput in{};

  size_t row = 0;

  splane->printf(" <type>: <key/hex> <hex/dec> "
                 "[Shift,Alt,Ctrl,Super,Hyper,Capslock,Numlock]\n");
}
