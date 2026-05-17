#include <chrono>
#include <memory>
#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <notcurses/notcurses.h>
#include <string_view>
#include <thread>

int main() {
  setlocale(LC_ALL, "");

  notcurses_options nc_opts{};

  nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_DRAIN_INPUT;

  ncpp::NotCurses nc(nc_opts);

  std::shared_ptr<ncpp::Plane> splane(nc.get_stdplane());

  std::shared_ptr<ncpp::Plane> mover_p =
      std::make_shared<ncpp::Plane>(1, 2, 1, 16);

  mover_p->set_bg_rgb8(255, 255, 255);
  mover_p->set_fg_rgb8(0, 255, 0);
  mover_p->set_base("", 0,
                    NCCHANNELS_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));
  mover_p->putstr(0, 0, "AA");

  std::string ALPHABET{"ABCDEFHIJKLMNOPQRSTUVWXYZABCDEFHIJKLMNOPQRSTUVWXYZ"};

  for (size_t i = 0; i < 25; ++i) {
    splane->putstr(10 + i, 10, ALPHABET.substr(i, 25).c_str());
    splane->putstr(10 + i, 35, ALPHABET.substr(i, 25).c_str());
  }

  nc.render();

  size_t x = 10;
  size_t y = 10;
  for (size_t i = 0; i < 100; ++i) {
    mover_p->move(x++, y++);

    nc.render();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
