#include <algorithm>
#include <cstddef>
#include <future>
#include <iterator>
#include <notcurses/nckeys.h>
#include <ranges>

#include "app_context.hpp"
#include "tools/draw.hpp"
#include "tools/locked.hpp"
#include "tools/perf.hpp"

#include "telemetry/driver_telemetry.hpp"

#include "core/input.hpp"

#include "types.hpp"
#include "ui/views.hpp"
#include "ui/widget.hpp"

using ThreadSafe::LockPair;

namespace UI::Views {

using UI::Table;

LeaderboardView::LeaderboardView() : table_info() {
  // table.get_mut()->set_dim(Table::Dim(0, 0));
  // table.get_mut()->column_sep = 1;

  // Configure the table
  {
    auto t = table_info.get_mut();
    t->table.no_backing_keys.clear();
    t->table.draw_cursor.test_and_set();
    t->table.draw_border.test_and_set();
    t->table.column_sep = 0;
    t->table.padding_top = 1;
    t->table.padding_left = 1;
  }

  using namespace Input;
  handler
      .register_callback(KeyWithMod('k', Modifier::NONE), "Move cursor up",
                         [this] {
                           table_info.get_mut()->table.move_cursor(
                               UI::CursorDirection::PREV);
                         })
      .register_callback(KeyWithMod('j', Modifier::NONE), "Move cursor down",
                         [this] {
                           table_info.get_mut()->table.move_cursor(
                               UI::CursorDirection::NEXT);
                         })
      .register_callback(
          KeyWithMod(NCKEY_SPACE, Modifier::NONE),
          "Toggle highlighting on the current row", [this] {
            auto t = table_info.get_mut();

            auto res = t->table.get_backing_key(table_cursor.load() %
                                                t->table.dim.get_const()->rows);
            if (!res.has_value()) {
              App::AppContext::Log().warn(
                  "Failed to match table's cursor to a backing key");
              return;
            }

            if (t->user_flagged_drivers.contains(res.value())) {
              t->user_flagged_drivers.erase(res.value());
              App::AppContext::Log().debug(
                  "Removed row with backing key \"{}\" to user-flagged drivers",
                  res.value());
            } else {
              t->user_flagged_drivers.emplace(res.value());
              App::AppContext::Log().debug(
                  "Added row with backing key \"{}\" to user-flagged drivers",
                  res.value());
            }
          });
}

void LeaderboardView::set_columns(std::vector<ColumnInfo> &&cs) {
  auto t = table_info.get_mut();
  t->cols = std::move(cs);
}

// Callback for async column-function application
std::vector<std::optional<UI::String>>
apply_column(LeaderboardView::ColumnInfo const &c,
             std::vector<Telemetry::DriverTelemetry> const &d) {
  return d | std::views::transform(c) |
         std::ranges::to<std::vector<std::optional<UI::String>>>();
}

std::expected<void, UI::RenderErr> LeaderboardView::update_and_render(
    ThreadSafe::LockPair<std::vector<Telemetry::DriverTelemetry> const>
        &&drivers,
    FocusState const focused, ncpp::Plane *const p) noexcept {

  if (drivers->size() == 0)
    return RenderErr(RenderErr::Source::VIEW, RenderErr::NOP,
                     "Leaderboard received no drivers to render");

  auto t = table_info.get_mut();

  auto dim = *t->table.dim.get_const();

  // Reconstruct columns if needed
  if (t->cols.size() != dim.cols) {
    App::AppContext::Log().debug("Leaderboard reconfiguring table to match {} "
                                 "drivers and {} column functions",
                                 drivers->size(), t->cols.size());

    t->table.set_dim(Table::Dim(drivers->size(), t->cols.size()));

    _swap_col =
        std::vector<std::optional<UI::String>>(drivers->size(), std::nullopt);

    // Reset the cursor to be a large number which is 0 mod num drivers
    table_cursor = drivers->size() * 100;

    // Configure the table columns' properties based on our columns
    size_t idx = 0;
    for (auto &c : t->cols) {
      auto props = t->table.column_props(idx++);
      props->align = c.align;
      props->draw_left_border = c.draw_left_border;
    }

    dim = *t->table.dim.get_const();

  } else if (drivers->size() != dim.rows) {
    App::AppContext::Log().debug("Leaderboard reconfiguring table to match {} "
                                 "drivers and {} column functions",
                                 drivers->size(), t->cols.size());
    // Reconfigure rows if neeeded. Already done if cols mismatched as
    // well -- only reached if just num drivers has changed.
    t->table.set_dim(Table::Dim(drivers->size(), t->cols.size()));

    dim = *t->table.dim.get_const();
  }

  if (focused)
    t->table.draw_cursor.test_and_set();
  else
    t->table.draw_cursor.clear();

  // Do nothing if we have no columns or no drivers
  if (!drivers->empty() && !t->cols.empty()) [[likely]] {
    // Let's try the fully synchronous method and profile it, improve it if
    // necessary

    // Calculate and draw columns
    {
      using OptStr = std::optional<UI::String>;
      using ColStrPair = std::pair<size_t, std::future<std::vector<OptStr>>>;

      App::PerfContext::StartLeaderboardPopulationTime();

      std::expected<void, WidgetErr> res;

      size_t col_idx = 0;

      for (auto &col : t->cols) {
        _swap_col.at(0) = col.draw_name ? col.name : " ";

        if (col.name == "Starred" && _update_starred.exchange(false))
          col.func = [stars = t->user_flagged_drivers](
                         Telemetry::DriverTelemetry const &d) -> UI::String {
            return stars.contains(d._car_num) ? L"★" : L" ";
          };

        std::transform(std::begin(*drivers), std::end(*drivers),
                       std::begin(_swap_col) + 1, col);
        res = t->table.swp_update_col(col_idx++, _swap_col);
      }

      App::PerfContext::StopLeaderboardPopulationTime();
    }

      // Reorder backing keys
      auto backing_reorder_res = t->table.set_backing_keys(
          std::ranges::fold_left(*drivers, std::vector<std::string>(),
                                 [](std::vector<std::string> &&v,
                                    Telemetry::DriverTelemetry const &d)
                                     -> std::vector<std::string> {
                                   v.emplace_back(d.get_car_num());
                                   return v;
                                 }));

      // Ensure backing reordering was successful
      if (!backing_reorder_res.has_value()) {
        switch (backing_reorder_res.error().kind) {
        case WidgetErr::OUT_OF_RANGE:
          return RenderErr(RenderErr::Source::WIDGET, RenderErr::TABLE_ERR,
                           "Leaderboard tried to set more backing keys than "
                           "the table supports");
          break;
        case WidgetErr::ACTION_DISABLED:
          return RenderErr(RenderErr::Source::WIDGET, RenderErr::TABLE_ERR,
                           "Leaderboard tried to set backing keys whilst "
                           "backing keys are disabled");
          break;
        }
      }
    }
  }
  // p->erase();
  TRY_DROP(t->table.apply(p));
  return always_render(std::move(t), focused, p);
}

// Lighter render
std::expected<void, UI::RenderErr>
LeaderboardView::update_and_render(FocusState const focused,
                                   ncpp::Plane *const p) noexcept {
  auto t = table_info.get_mut();

  if (focused)
    t->table.draw_cursor.test_and_set();
  else
    t->table.draw_cursor.clear();

  TRY_DROP(t->table.apply(p));

  return always_render(std::move(t), focused, p);
}

// Steps to always render
std::expected<void, UI::RenderErr> LeaderboardView::always_render(
    [[maybe_unused]] ThreadSafe::LockPair<TableInfo> &&t,
    FocusState const focused, ncpp::Plane *const p) noexcept {

  // auto dim = *t->table.dim.get_const();

  // {
  //   auto h_rows = highlight_rows.get_const();

  //   for (auto const &[idx, color] : *h_rows) {
  //     auto ncpp_color =
  //         Tools::Draw::mask_pal_channels(p, Tools::Draw::Target::BG, color);
  //     p->stain((idx % dim.rows) + 1, 1, 1, p->get_dim_x() - 1, ncpp_color,
  //              ncpp_color, ncpp_color, ncpp_color);
  //   }
  // }

  // {
  //   auto res = t->table.batch_key_lookup(t->user_flagged_drivers);
  //   if (!res.has_value()) {
  //     return UI::RenderErr(UI::RenderErr::Source::VIEW,
  //                          UI::RenderErr::TABLE_ERR,
  //                          "Failed to batch-lookup user-flagged drivers");
  //   }

  //   auto color = Tools::Draw::mask_pal_channels(p, Tools::Draw::Target::BG,
  //                                               UI::Color::BLUE_SOFT);
  //   for (size_t idx : std::move(res.value()))
  //     p->stain(idx + 1, 1, 1, p->get_dim_x() - 1, color, color, color,
  //     color);
  // }

  if (_prev_focus != focused)
    Tools::Draw::border_with_title(
        p, "Leaderboard", focused ? UI::Color::CYAN_SOFT : UI::Color::NONE);

  _prev_focus = focused;

  return {};
}

bool LeaderboardView::take_input(Input::KeyWithMod const &k) {
  if (!handler.has_registered(k))
    return false;

  handler.handle_input(k);

  return true;
}

}; // namespace UI::Views
