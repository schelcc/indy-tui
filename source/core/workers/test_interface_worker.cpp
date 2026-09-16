
#include <memory>

#include "core/workers.hpp"

#include "core/time.hpp"
#include "tools/logger.hpp"
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

  UI::Table table{};

  table.set_dim(UI::Table::Dim{4, 3});

  std::expected<void, UI::ViewErr> res;

  // Set column names
  res = table.update_row(0, {"Qty.", "Value", "Empty"});

  // Set row names
  res = table.update_col(0, 1, {"Cycles", "Cycles quot. 5", "Cycles quot. 10"});

  size_t cnt = 0;

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_HZ;

    // Update column values
    res = table.update_col(1, 1, {cnt, cnt / 5, cnt / 10});

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
