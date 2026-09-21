#include "driver_telemetry.hpp"
#include "logger.hpp"
#include "ui/views.hpp"
#include "widget.hpp"
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
    LockPair<UI::Table &> &&locked_table,
    LockPair<std::vector<ColumnInfo> &> &&locked_columns,
    LockPair<size_t &> &&locked_num) {

  // Reset dimensions, adding 1 row to account for column titles
  locked_table->set_dim(Table::Dim(*locked_num, locked_columns->size()));

  // Set alignment based on the columns
  for (auto [idx, c] : std::views::enumerate(*locked_columns))
    locked_table->column_props(idx)->align = c.align;
}

void LeaderboardView::update_and_render(
    ThreadSafe::LockPair<std::vector<Telemetry::DriverTelemetry> const &>
        &&drivers,
    std::shared_ptr<ncpp::Plane> p) {
  // TODO - Figure out a better spot for this
  set_num_drivers(drivers->size());

  // Do nothing if we have no columns or no drivers
  if (drivers->empty() || _cols.get_const()->empty() ||
      (*_num_drivers.get_const() == 0))
    return;

  // Let's try the fully synchronous method and profile it, improve it if
  // necessary

  auto locked_table = table.get_mut();

  assert(locked_table->get_dim().cols == _cols.get_const()->size());
  assert(locked_table->get_dim().rows == *_num_drivers.get_const());

  for (auto const &[idx, col] : std::views::enumerate(*_cols.get_const())) {
    auto _ = locked_table->update_col(
        idx, *drivers | std::views::transform(col) | std::views::as_rvalue |
                 std::ranges::to<std::vector<std::optional<UI::String>>>());

    if (!_.has_value())
      Tools::Log::Warn("Failed to render columns");
  }

  p->erase();
  locked_table->apply(p);
}

}; // namespace UI::Views
