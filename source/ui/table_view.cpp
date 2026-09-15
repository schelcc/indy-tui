#include "string.hpp"
#include "ui/widget.hpp"
#include <algorithm>
#include <iterator>
#include <mutex>
#include <ncpp/Plane.hh>
#include <ranges>

namespace UI {

TableView::Dim TableView::get_dim() const noexcept {
  std::shared_lock lock(_mtx);
  const size_t col_size = (_table.size() > 0) ? _table.at(0).size() : 0;

  assert(std::all_of(
      std::cbegin(_table), std::cend(_table),
      [col_size](auto const &col) { return col.size() == col_size; }));

  return Dim(_table.size(), col_size);
}

void TableView::set_dim(TableView::Dim const &d) noexcept {
  // Do nothing if same dimensions
  if (get_dim() == d)
    return;

  std::unique_lock lock(_mtx);
  _table = std::vector<std::vector<std::optional<UI::String>>>(
      d.rows, std::vector<std::optional<UI::String>>(
                  d.cols, std::optional<UI::String>{}));

  _col_widths = std::vector<size_t>(d.cols, 0);

  return;
}

std::expected<void, ViewErr> TableView::set_at(size_t const row,
                                               size_t const col,
                                               UI::String &&str) noexcept {
  auto const d = get_dim();

  if ((col >= d.cols) || (row >= d.rows))
    return ViewErr(ViewErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);

  _col_widths.at(col) = std::max(_col_widths.at(col), str.size());

  _table.at(row).at(col) = std::move(str);

  return {};
}

std::expected<void, ViewErr> TableView::update_row(
    size_t const row,
    std::vector<std::optional<UI::String>> &&new_row) noexcept {
  auto const d = get_dim();

  if ((row >= d.rows) || (new_row.size() > d.cols))
    return ViewErr(ViewErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);

  size_t idx = 0;
  for (auto const &s : new_row) {
    _col_widths.at(idx) = std::max(
        s.transform([](UI::String const &_s) { return _s.size(); }).value_or(0),
        _col_widths.at(idx));
    idx++;
  }

  _table.at(row) = std::move(new_row);

  return {};
}

std::expected<void, ViewErr> TableView::update_row(
    size_t const row, size_t const col_offset,
    std::vector<std::optional<UI::String>> &&new_row) noexcept {
  auto const d = get_dim();

  if ((row >= d.rows) || ((new_row.size() + col_offset) > d.cols))
    return ViewErr(ViewErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);

  size_t idx = col_offset;
  for (auto const &s : new_row) {
    _col_widths.at(idx) = std::max(
        s.transform([](UI::String const &_s) { return _s.size(); }).value_or(0),
        _col_widths.at(idx));
    idx++;
  }

  std::move(std::begin(new_row), std::end(new_row),
            std::begin(_table.at(row)) + col_offset);

  return {};
}

std::expected<void, ViewErr> TableView::clear_at(size_t const row,
                                                 size_t const col) noexcept {
  auto const d = get_dim();

  if ((col >= d.cols) || (row >= d.rows))
    return ViewErr(ViewErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);
  _table.at(row).at(col) = std::optional<UI::String>{};

  return {};
}

void TableView::apply(std::shared_ptr<ncpp::Plane> p) const noexcept {
  std::shared_lock lock(_mtx);

  /*
  A stupidly <algorithm> way to do it:

  std::vector<size_t> accumulated_offsets{};
  std::transform(std::cbegin(_col_widths), std::cend(_col_widths),
                 std::back_inserter(accumulated_offsets), [](size_t const w) {
                   static size_t sum = 0;
                   return (sum += w) - w;
                 });

  size_t row_idx = 0;
  std::ranges::for_each(
      _table, [p, accumulated_offsets,
               &row_idx](std::vector<std::optional<UI::String>> const &row) {
        std::ranges::for_each(
            std::ranges::zip_view(row, accumulated_offsets),
            [p](auto const &c) {
              auto [col_str, offset] = c;
              col_str.value_or(UI::String("--")).apply_to_plane(p, 0, offset);
            });
        row_idx++;
      });
  */

  size_t row_idx = 0;
  for (auto const &row : _table) {
    size_t col_offset = 0;
    size_t col_idx = 0;
    for (auto const &col : row) {
      col.value_or(UI::String("--")).apply_to_plane(p, row_idx, col_offset);
      col_offset += (_col_widths.at(col_idx++) + column_sep);
    }
    row_idx++;
  }
}

}; // namespace UI
