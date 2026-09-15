
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

  // Set column names
  res = table.update_row(
      0, {UI::String("Qty."), UI::String("Value"), UI::String("Empty")});

  // Set row names
  res = table.update_col(0, 1,
                         {UI::String("Cycles"), UI::String("Cycles quot. 5"),
                          UI::String("Cycles quot. 10")});

  size_t cnt = 0;

  while (!stop_tok.stop_requested()) {
    auto render_start = Time::Clock::now();
    auto block_until = render_start + MAX_REDRAW_HZ;

    // Update column values
    res = table.update_col(1, 1,
                           {UI::String(std::to_string(cnt)),
                            UI::String(std::to_string(cnt / 5)),
                            UI::String(std::to_string(cnt / 10))});

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
