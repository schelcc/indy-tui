#include "draw.hpp"
#include "driver_telemetry.hpp"
#include "locked.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "ui/views.hpp"
#include "widget.hpp"
#include <execution>
#include <future>
#include <iterator>
#include <ranges>

using ThreadSafe::LockPair;

namespace UI::Views {

using UI::Table;

LeaderboardView::LeaderboardView() : table(), _cols(), _num_drivers(0) {
  table.get_mut()->set_dim(Table::Dim(0, 0));
  table.get_mut()->column_sep = 1;
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

  // Do nothing if we have no columns or no drivers
  if (drivers->empty() || _cols.get_const()->empty() ||
      (*_num_drivers.get_const() == 0))
    return;

  // Let's try the fully synchronous method and profile it, improve it if
  // necessary

  auto locked_table = table.get_mut();

  auto dim = locked_table->get_dim();

  assert(dim.cols == _cols.get_const()->size());
  assert(dim.rows == *_num_drivers.get_const());
  assert(dim.rows == drivers->size());

  using OptStr = std::optional<UI::String>;
  using ColStrPair = std::pair<size_t, std::future<std::vector<OptStr>>>;

  App::PerfContext::StartLeaderboardPopulationTime();

  // Create vec of pairs of position and async results
  std::vector<ColStrPair> col_strs{};
  col_strs.reserve(drivers->size() + 1);

  auto cols = _cols.get_const();

  // For each column, add to col_strs an async call which applies the col func
  // to the drivers vec
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

  p->erase();
  locked_table->apply(p);
  Tools::Draw::border_with_title(p, "Leaderboard",
                                 focused ? Tools::Draw::PaletteColors::CYAN_SOFT
                                         : Tools::Draw::PaletteColors::NONE);
}

}; // namespace UI::Views
