#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <mutex>
#include <ranges>

#include <ncpp/Plane.hh>
#include <stdexcept>

#include "locked.hpp"
#include "ui/string.hpp"
#include "ui/widget.hpp"

namespace UI {

void Table::Column::recalculate_width(UI::String const &default_str) {
  max_width = std::ranges::max(
      rows |
      std::views::transform([&default_str](std::optional<UI::String> const &s) {
        return s.value_or(default_str).size();
      }));
}

Table::Dim Table::get_dim() const noexcept {
  std::shared_lock lock(_mtx);

  const size_t row_size = (_table.size() > 0) ? _table.at(0).rows.size() : 0;

  assert(std::ranges::all_of(_table, [row_size](auto const &col) {
    return col.rows.size() == row_size;
  }));

  return Dim(row_size, _table.size());
}

void Table::set_dim(Table::Dim const &d) noexcept {
  // Do nothing if same dimensions
  if (get_dim() == d)
    return;

  std::unique_lock lock(_mtx);
  _table = std::vector<Column>(
      d.cols,
      Column(std::vector<std::optional<UI::String>>(d.rows, std::nullopt)));

  return;
}

std::expected<void, WidgetErr> Table::set_at(size_t const row, size_t const col,
                                             UI::String &&str) noexcept {
  auto const d = get_dim();

  if ((col >= d.cols) || (row >= d.rows))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);

  _table.at(col).max_width = std::max(_table.at(col).max_width, str.size());
  _table.at(col).rows.at(row) = std::move(str);

  return {};
}

std::expected<void, WidgetErr>
Table::update_row(size_t const row,
                  std::vector<std::optional<UI::String>> &&new_row) noexcept {
  auto const d = get_dim();

  if ((row >= d.rows) || (new_row.size() > d.cols))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);

  size_t col_idx = 0;
  for (std::optional<UI::String> s : std::move(new_row)) {
    _table.at(col_idx).max_width = std::max(
        _table.at(col_idx).max_width, s.transform([](UI::String const &_s) {
                                         return _s.size();
                                       }).value_or(0));

    _table.at(col_idx).rows.at(row) = std::move(s);

    col_idx++;
  }

  return {};
}

std::expected<void, WidgetErr>
Table::update_row(size_t const row, size_t const col_offset,
                  std::vector<std::optional<UI::String>> &&new_row) noexcept {
  auto const d = get_dim();

  if ((row >= d.rows) || ((new_row.size() + col_offset) > d.cols))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);

  size_t col_idx = col_offset;
  for (std::optional<UI::String> s : std::move(new_row)) {
    _table.at(col_idx).max_width = std::max(
        _table.at(col_idx).max_width, s.transform([](UI::String const &_s) {
                                         return _s.size();
                                       }).value_or(0));

    _table.at(col_idx).rows.at(row) = std::move(s);

    col_idx++;
  }

  return {};
}

std::expected<void, WidgetErr>
Table::update_col(size_t const col,
                  std::vector<std::optional<UI::String>> &&new_col) noexcept {
  auto const d = get_dim();

  if ((col >= d.cols) || (new_col.size() > d.rows))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);
  _table.at(col).rows = std::move(new_col);
  _table.at(col).recalculate_width(empty_cell);

  return {};
}

std::expected<void, WidgetErr>
Table::update_col(size_t const col, size_t const row_offset,
                  std::vector<std::optional<UI::String>> &&new_col) noexcept {
  auto const d = get_dim();

  if ((col >= d.cols) || ((new_col.size() + row_offset) > d.rows))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);

  std::move(std::begin(new_col), std::end(new_col),
            std::begin(_table.at(col).rows) + static_cast<long>(row_offset));

  _table.at(col).recalculate_width(empty_cell);

  return {};
}

ThreadSafe::LockPair<Table::Column::Props &>
Table::column_props(size_t const col) {
  if (col > _table.size())
    throw std::out_of_range("Column not in range");

  return {std::unique_lock(_mtx), _table.at(col).props};
}

std::expected<void, WidgetErr> Table::clear_at(size_t const row,
                                               size_t const col) noexcept {
  auto const d = get_dim();

  if ((col >= d.cols) || (row >= d.rows))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);
  _table.at(col).rows.at(col) = std::nullopt;

  return {};
}

void Table::apply(std::shared_ptr<ncpp::Plane> p) const noexcept {
  std::shared_lock lock(_mtx);

  size_t col_pos = 1;
  for (auto const &col : _table) {
    size_t row_offset = 1;

    for (auto const &row : col.rows) {
      if (col.props.align == Align::LEFT) {
        row.value_or(empty_cell).apply_to_plane(p, row_offset, col_pos);
      } else {
        auto row_s = row.value_or(empty_cell);
        row_s.apply_to_plane(p, row_offset,
                             col_pos +
                                 ((col.props.align == Align::RIGHT)
                                      ? col.max_width - row_s.size()
                                      : (col.max_width - row_s.size()) / 2));
      }
      row_offset++;
    }
    col_pos += (col.max_width + column_sep);
  }
}

void Table::clear() noexcept {
  auto d = get_dim();
  std::unique_lock lock(_mtx);
  _table = std::vector<Column>(
      d.cols,
      Column(std::vector<std::optional<UI::String>>(d.rows, std::nullopt)));
}
}; // namespace UI
