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

  // We'll need the backing keys for the leaderboard
  table_info.get_mut()->table.no_backing_keys.clear();

  using namespace Input;
  handler
      .register_callback(KeyWithMod('k', Modifier::NONE), "Move cursor up",
                         [this] { table_cursor--; })
      .register_callback(KeyWithMod('j', Modifier::NONE), "Move cursor down",
                         [this] { table_cursor++; })
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
    bool const focused, ncpp::Plane *const p) noexcept {

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

    // Reset the cursor to be a large number which is 0 mod num drivers
    table_cursor = drivers->size() * 100;

    // Configure the table columns' properties based on our columns
    size_t idx = 0;
    for (auto &c : t->cols)
      t->table.column_props(idx++)->align = c.align;

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

  // Do nothing if we have no columns or no drivers
  if (!drivers->empty() && !t->cols.empty()) [[likely]] {
    // Let's try the fully synchronous method and profile it, improve it if
    // necessary

    // Calculate and draw columns
    {
      using OptStr = std::optional<UI::String>;
      using ColStrPair = std::pair<size_t, std::future<std::vector<OptStr>>>;

      App::PerfContext::StartLeaderboardPopulationTime();

      // Create vec of pairs of position and async results
      std::vector<ColStrPair> col_strs{};
      col_strs.reserve(drivers->size() + 1);

      // For each column, add to col_strs an async call which applies the col
      // func to the drivers vec
      size_t pos = 0;
      std::transform(
          std::cbegin(t->cols), std::cend(t->cols),
          std::back_inserter(col_strs),
          [&drivers, &pos](ColumnInfo const &col_func) {
            return std::make_tuple(
                pos++, std::async(std::launch::async | std::launch::deferred,
                                  apply_column, col_func, std::cref(*drivers)));
          });

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

      // Retrieve the value from the generated futures and populate the table
      // accordingly
      for (auto &[col_idx, val] : col_strs) {
        [[maybe_unused]] auto res =
            t->table.update_col(col_idx, std::move(val).get());
      }

      App::PerfContext::StopLeaderboardPopulationTime();
    }
  }
  p->erase();
  TRY_DROP(t->table.apply(p));
  return always_render(std::move(t), focused, p);
}

// Lighter render
std::expected<void, UI::RenderErr>
LeaderboardView::update_and_render(bool const focused,
                                   ncpp::Plane *const p) noexcept {
  auto t = table_info.get_mut();

  auto rows = t->table.dim.get_const()->rows;
  TRY_DROP(t->table.apply_row(p, (table_cursor.load() - 1) % rows));
  TRY_DROP(t->table.apply_row(p, table_cursor.load() % rows));
  TRY_DROP(t->table.apply_row(p, (table_cursor.load() + 1) % rows));

  return always_render(std::move(t), focused, p);
}

// Steps to always render
std::expected<void, UI::RenderErr>
LeaderboardView::always_render(ThreadSafe::LockPair<TableInfo> &&t,
                               bool const focused,
                               ncpp::Plane *const p) noexcept {
  auto dim = *t->table.dim.get_const();

  // {
  //   auto h_rows = highlight_rows.get_const();

  //   for (auto const &[idx, color] : *h_rows) {
  //     auto ncpp_color =
  //         Tools::Draw::mask_pal_channels(p, Tools::Draw::Target::BG, color);
  //     p->stain((idx % dim.rows) + 1, 1, 1, p->get_dim_x() - 1, ncpp_color,
  //              ncpp_color, ncpp_color, ncpp_color);
  //   }
  // }

  {
    auto res = t->table.batch_key_lookup(t->user_flagged_drivers);
    if (!res.has_value()) {
      return UI::RenderErr(UI::RenderErr::Source::VIEW,
                           UI::RenderErr::TABLE_ERR,
                           "Failed to batch-lookup user-flagged drivers");
    }

    auto color = Tools::Draw::mask_pal_channels(p, Tools::Draw::Target::BG,
                                                UI::Color::BLUE_SOFT);
    for (size_t idx : std::move(res.value()))
      p->stain(idx + 1, 1, 1, p->get_dim_x() - 1, color, color, color, color);
  }

  // Draw cursor, if focused
  if (focused) {
    auto color = Tools::Draw::mask_pal_channels(p, Tools::Draw::Target::BG,
                                                UI::Color::BLUE_SOFT);
    p->stain((table_cursor.load() % dim.rows) + 1, 1, 1, p->get_dim_x() - 1,
             color, color, color, color);
  }

  Tools::Draw::border_with_title(
      p, "Leaderboard", focused ? UI::Color::CYAN_SOFT : UI::Color::NONE);

  return {};
}

bool LeaderboardView::take_input(Input::KeyWithMod const &k) {
  if (!handler.has_registered(k))
    return false;

  handler.handle_input(k);

  return true;
}

}; // namespace UI::Views
