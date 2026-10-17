#include "draw.hpp"
#include "driver_telemetry.hpp"
#include "input.hpp"
#include "locked.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "ui/views.hpp"
#include "widget.hpp"
#include <cstddef>
#include <execution>
#include <future>
#include <iterator>
#include <limits>
#include <notcurses/nckeys.h>
#include <ranges>

using ThreadSafe::LockPair;

namespace UI::Views {

using UI::Table;

LeaderboardView::LeaderboardView() : table(), _cols(), _num_drivers(0) {
  table.get_mut()->set_dim(Table::Dim(0, 0));
  table.get_mut()->column_sep = 1;

  using namespace Input;
  handler
      .register_callback(KeyWithMod('k', Modifier::NONE), "Move cursor up",
                         [this] { table_cursor--; })
      .register_callback(KeyWithMod('j', Modifier::NONE), "Move cursor down",
                         [this] { table_cursor++; })
      .register_callback(
          KeyWithMod(NCKEY_SPACE, Modifier::NONE),
          "Toggle highlighting on the current row", [this] {
            [[maybe_unused]] std::expected<void, WidgetErr> res;
            auto d = table.get_const()->get_dim();

            auto h_rows = highlight_rows.get_mut();
            auto row = table_cursor.load();

            auto it = std::ranges::find_if(*h_rows, [row, d](auto const &r) {
              return r.idx == (row % d.rows);
            });
            if (it == h_rows->end()) {
              h_rows->emplace_back(row % d.rows, UI::Color::CYAN_SOFT);
            } else {
              h_rows->erase(it);
            }
          });
}

void LeaderboardView::set_columns(std::vector<ColumnInfo> &&cs) {
  auto locked_columns = _cols.get_mut();
  *locked_columns = std::move(cs);

  reconstruct_table(table.get_mut(), std::move(locked_columns),
                    _num_drivers.get_mut());
}

void LeaderboardView::set_num_drivers(size_t const num) {
  // Do nothing if would not change
  if (num == *_num_drivers.get_const())
    return;

  auto locked_num = _num_drivers.get_mut();
  *locked_num = num;

  reconstruct_table(table.get_mut(), _cols.get_mut(), std::move(locked_num));
}

void LeaderboardView::reconstruct_table(
    LockPair<UI::Table> &&locked_table,
    LockPair<std::vector<ColumnInfo>> &&locked_columns,
    LockPair<size_t> &&locked_num) {

  // Reset dimensions, adding 1 row to account for column titles
  locked_table->set_dim(Table::Dim(*locked_num, locked_columns->size()));

  // Default the cursor position to be in the middle of size_t's range, at the
  // closest equiv. 0 mod rows
  table_cursor = *locked_num * 100;

  // Set alignment based on the columns
  for (auto [idx, c] : std::views::enumerate(*locked_columns))
    locked_table->column_props(idx)->align = c.align;
}

// Callback for async column-function application
std::vector<std::optional<UI::String>>
apply_column(LeaderboardView::ColumnInfo const &c,
             std::vector<Telemetry::DriverTelemetry> const &d) {
  return d | std::views::transform(c) |
         std::ranges::to<std::vector<std::optional<UI::String>>>();
}

void LeaderboardView::update_and_render(
    ThreadSafe::LockPair<std::vector<Telemetry::DriverTelemetry> const>
        &&drivers,
    bool const focused, std::shared_ptr<ncpp::Plane> p) {
  // TODO - Figure out a better spot for this
  set_num_drivers(drivers->size());

  auto locked_table = table.get_mut();

  // Do nothing if we have no columns or no drivers
  if (!drivers->empty() && !_cols.get_const()->empty() &&
      (*_num_drivers.get_const() > 0)) [[likely]] {

    // Let's try the fully synchronous method and profile it, improve it if
    // necessary

    auto dim = locked_table->get_dim();

    assert(dim.cols == _cols.get_const()->size());
    assert(dim.rows == *_num_drivers.get_const());
    assert(dim.rows == drivers->size());

    // Calculate and draw columns
    {
      using OptStr = std::optional<UI::String>;
      using ColStrPair = std::pair<size_t, std::future<std::vector<OptStr>>>;

      App::PerfContext::StartLeaderboardPopulationTime();

      // Create vec of pairs of position and async results
      std::vector<ColStrPair> col_strs{};
      col_strs.reserve(drivers->size() + 1);

      auto cols = _cols.get_const();

      // For each column, add to col_strs an async call which applies the col
      // func to the drivers vec
      size_t pos = 0;
      std::transform(
          std::begin(*cols), std::end(*cols), std::back_inserter(col_strs),
          [&drivers, &pos](ColumnInfo const &col_func) {
            return std::make_tuple(
                pos++, std::async(apply_column, col_func, std::cref(*drivers)));
          });

      // Retrieve the value from the generated futures and populate the table
      // accordingly
      for (auto &[pos, val] : col_strs) {
        [[maybe_unused]] auto res =
            locked_table->update_col(pos, std::move(val).get());
      }

      App::PerfContext::StopLeaderboardPopulationTime();
    }
  }
  p->erase();
  locked_table->apply(p);
  always_render(std::move(locked_table), focused, p);
}

// Lighter render
void LeaderboardView::update_and_render(bool const focused,
                                        std::shared_ptr<ncpp::Plane> p) {
  auto locked_table = table.get_mut();

    {
      auto h_rows = highlight_rows.get_const();
      for (auto const &[idx, color] : *h_rows) {
        auto ncpp_color =
            Tools::Draw::mask_pal_channels(p, Tools::Draw::Target::BG, color);
        p->stain((idx % dim.rows) + 1, 1, 1, p->get_dim_x() - 1, ncpp_color,
                 ncpp_color, ncpp_color, ncpp_color);
      }
    }

// Steps to always render
void LeaderboardView::always_render(
    ThreadSafe::LockPair<UI::Table> &&locked_table, bool const focused,
    std::shared_ptr<ncpp::Plane> p) {
  auto dim = locked_table->get_dim();

  {
    auto h_rows = highlight_rows.get_const();
    for (auto const &[idx, color] : *h_rows) {
      auto ncpp_color =
          Tools::Draw::mask_pal_channels(p, Tools::Draw::Target::BG, color);
      p->stain((idx % dim.rows) + 1, 1, 1, p->get_dim_x() - 1, ncpp_color,
               ncpp_color, ncpp_color, ncpp_color);
    }
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
}

bool LeaderboardView::take_input(Input::KeyWithMod const &k) {
  if (!handler.has_registered(k))
    return false;

  handler.handle_input(k);

  return true;
}

}; // namespace UI::Views
