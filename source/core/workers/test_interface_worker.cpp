
#include <memory>

#include "columns.hpp"
#include "core/workers.hpp"

#include "core/time.hpp"
#include "draw.hpp"
#include "tools/logger.hpp"
#include "ui.hpp"
#include "ui/layout.hpp"
#include "ui/widget.hpp"

namespace Workers {

void TestInterfaceWorker::operator()(std::stop_token stop_tok) {
  Tools::Log::Debug("Leaderboard worker instantiated", "LEADERBOARD");

  std::shared_ptr<ncpp::Plane> std_plane(nc.get_stdplane());

  Time::TimePoint last_tick;
  last_tick = Time::Clock::now();

  // UI::LayoutContainer<UI::LayoutDirection::HORIZONTAL> base_container(
  //     std_plane);
  // auto &w = base_container.add_widget<UI::TextBox>(1);

  UI::TableView table{};

  table.set_dim(UI::TableView::Dim{4, 3});

  std::expected<void, UI::ViewErr> res;

  res = table.set_at(1, 0, UI::String("Cycles"));
  res = table.set_at(2, 0, UI::String("Cycles quot. 5"));
  res = table.set_at(3, 0, UI::String("Cycles quot. 10"));

  size_t cnt = 0;

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_HZ;

    res = table.set_at(0, 1, UI::String(std::to_string(cnt)));
    res = table.set_at(1, 1, UI::String(std::to_string(cnt / 5)));
    res = table.set_at(2, 1, UI::String(std::to_string(cnt / 10)));

    table.apply(std_plane);

    nc.render();

    std_plane->erase();

    std::this_thread::sleep_until(block_until);

    Time::Duration::DblMilliSec gap = (Time::Clock::now() - render_start);

    last_tick = Time::Clock::now();
    cnt++;
  }

  Tools::Log::Debug("Exited leaderboard loop", "LEADERBOARD");
  nc.stop();
}

}; // namespace Workers
