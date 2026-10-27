#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <mutex>

#include <ncpp/Plane.hh>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "app_context.hpp"
#include "tools/locked.hpp"

#include "types.hpp"
#include "ui/string.hpp"
#include "ui/widget.hpp"

namespace UI {

bool Table::Column::recalculate_width(UI::String const &default_str) {

  auto pre_width = max_width;

  max_width = default_str.size();

  for (std::optional<UI::String> const &s : rows) {
    max_width =
        s.has_value() ? std::max(max_width, s.value().size()) : max_width;
  }

  // Never let max_width be zero
  max_width = (max_width > 0) ? max_width : 1;

  return max_width != pre_width;
}

bool Table::Column::recalculate_width(size_t const check_width) {
  auto pre_width = max_width;

  max_width = std::max(check_width, max_width);

  return max_width != pre_width;
}

void Table::rebuild_columns() noexcept {
  App::AppContext::Log().debug("Rebuilding table columns");

  _col_planes.clear();
  _col_planes.reserve(_table.size());

  size_t x_off = 1 + padding_left;
  size_t const y_off = 1 + padding_top;
  size_t const ylen = _plane->get_dim_y() - y_off - 1;
  size_t const col_sep = column_sep.load();

  for (auto &col : _table) {
    _col_planes.emplace_back(_plane, ylen, col.max_width, y_off, x_off);
    x_off += col.max_width + col_sep;
  }

  _col_planes.shrink_to_fit();
}

void Table::set_dim(Table::Dim const &d) noexcept {
  // Do nothing if same dimensions
  auto old_dim = dim.get_mut();

  if (*old_dim == d) {
    App::AppContext::Log().debug("Idempotent table transformation");
    return;
  }

  App::AppContext::Log().debug("Resizing table to {} rows x {} cols", d.rows,
                               d.cols);

  std::unique_lock lock(_mtx);
  _table = std::vector<Column>(d.cols, d.rows);

  *old_dim = d;

  if (!no_backing_keys.test())
    _backing_keys = std::vector<std::string>(d.rows, "");

  return;
}

std::expected<void, WidgetErr> Table::set_at(size_t const row, size_t const col,
                                             UI::String &&str) noexcept {
  auto const d = *dim.get_const();

  if ((col >= d.cols) || (row >= d.rows)) {
    App::AppContext::Log().warn(
        "Tried to update table of size ({} rows, {} cols) at ({}, {})", d.rows,
        d.cols, row, col);

    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  std::unique_lock lock(_mtx);

  _needs_rebuild =
      _table.at(col).recalculate_width(str.size()) || _needs_rebuild;

  _table.at(col).rows.at(row) = std::move(str);

  return {};
}

std::expected<std::string, WidgetErr>
Table::get_backing_key(size_t const row) const noexcept {
  std::shared_lock lock(_mtx);
  auto d = *dim.get_const();

  if (no_backing_keys.test()) {
    App::AppContext::Log().warn(
        "Attempted to retrieve backing key when backing keys disabled");
    return WidgetErr(WidgetErr::ACTION_DISABLED);
  }

  if (row >= d.rows) {
    App::AppContext::Log().warn(
        "Attempted to retrieve backing key ({}) out of range ({})", row,
        d.rows);
    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  return _backing_keys.at(row);
}

std::expected<size_t, WidgetErr>
Table::get_backing_key_row(std::string_view const key) const noexcept {
  std::shared_lock lock(_mtx);

  if (no_backing_keys.test()) {
    App::AppContext::Log().warn(
        "Attempted to retrieve backing key when backing keys disabled");
    return WidgetErr(WidgetErr::ACTION_DISABLED);
  }

  auto row = std::ranges::find_if(
      _backing_keys, [&key](std::string const &s) { return s == key; });

  if (row == std::end(_backing_keys)) {
    App::AppContext::Log().warn(
        "Attempted to lookup backing key \"{}\" which could not be found", key);
    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  return row - std::cbegin(_backing_keys);
}

std::expected<std::vector<size_t>, WidgetErr> Table::batch_key_lookup(
    std::unordered_set<std::string> const &keys) const noexcept {
  std::shared_lock lock(_mtx);

  if (no_backing_keys.test()) {
    App::AppContext::Log().warn(
        "Attempted to batch-retrieve keys when backing keys disabled");
    return WidgetErr(WidgetErr::ACTION_DISABLED);
  }

  return std::ranges::fold_left(std::views::enumerate(_backing_keys),
                                std::vector<size_t>(),
                                [&keys](std::vector<size_t> &&v, auto p) {
                                  auto [idx, s] = p;
                                  if (keys.contains(s))
                                    v.emplace_back(idx);
                                  return v;
                                });
}

std::expected<void, WidgetErr>
Table::set_backing_key(size_t const row, std::string_view const key) noexcept {
  auto const d = *dim.get_const();

  if (no_backing_keys.test()) {
    App::AppContext::Log().warn(
        "Attempted to set backing key when backing keys disabled");
    return WidgetErr(WidgetErr::ACTION_DISABLED);
  }

  if (row >= d.rows) {
    App::AppContext::Log().warn(
        "Attempted to set backing key ({}) out of range ({})", row, d.rows);
    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  _backing_keys.at(row) = key;

  return {};
}

std::expected<void, WidgetErr>
Table::set_backing_keys(std::vector<std::string> &&keys,
                        size_t const row_offset) noexcept {
  auto const d = *dim.get_const();

  if (no_backing_keys.test()) {
    App::AppContext::Log().warn(
        "Attempted to set backing key when backing keys disabled");
    return WidgetErr(WidgetErr::ACTION_DISABLED);
  }

  if (row_offset >= d.rows) {
    App::AppContext::Log().warn("Attempted to set backing keys at row offset "
                                "({}), which out of range ({})",
                                row_offset, d.rows);
    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  if ((row_offset + keys.size()) > d.rows) {
    App::AppContext::Log().warn("Attempted to set {} backing keys at row "
                                "offset {} when offset update can "
                                "contain at most {} rows",
                                keys.size(), row_offset, d.rows - row_offset);
    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  std::move(std::begin(keys), std::end(keys), std::begin(_backing_keys));

  return {};
}

std::expected<void, WidgetErr>
Table::update_row(size_t const row,
                  std::vector<std::optional<UI::String>> &&new_row) noexcept {
  auto const d = *dim.get_const();

  if ((row >= d.rows) || (new_row.size() > d.cols)) {
    std::string why = "Tried to update table row outside table's range";

    if (row >= d.rows)
      why +=
          std::format(", row {} requested when max row is {}", row, d.rows - 1);

    if (new_row.size() > d.cols)
      why += std::format(", new row contains {} columns when table contains at "
                         "most {} columns",
                         new_row.size(), d.cols);

    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  std::unique_lock lock(_mtx);

  size_t col_idx = 0;
  for (std::optional<UI::String> s : std::move(new_row)) {
    auto &target_col = _table.at(col_idx);

    _needs_rebuild =
        target_col.recalculate_width(s.value_or(empty_cell)) || _needs_rebuild;

    target_col.rows.at(row) = std::move(s);

    col_idx++;
  }

  return {};
}

std::expected<void, WidgetErr>
Table::update_row(size_t const row, size_t const col_offset,
                  std::vector<std::optional<UI::String>> &&new_row) noexcept {
  auto const d = *dim.get_const();

  if ((row >= d.rows) || ((new_row.size() + col_offset) > d.cols)) {
    std::string why = "Tried to update table row outside table's range";

    if (row >= d.rows)
      why +=
          std::format(", row {} requested when max row is {}", row, d.rows - 1);

    if ((new_row.size() + col_offset) > d.cols)
      why += std::format(", new row contains {} columns when offset update can "
                         "contain at most {} columns",
                         new_row.size(), d.cols - col_offset);

    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  std::unique_lock lock(_mtx);

  size_t col_idx = col_offset;
  for (std::optional<UI::String> s : std::move(new_row)) {
    auto &target_col = _table.at(col_idx);

    _needs_rebuild =
        target_col.recalculate_width(s.value_or(empty_cell)) || _needs_rebuild;

    target_col.rows.at(row) = std::move(s);

    col_idx++;
  }

  return {};
}

std::expected<void, WidgetErr>
Table::update_col(size_t const col,
                  std::vector<std::optional<UI::String>> &&new_col) noexcept {
  auto const d = *dim.get_const();

  if ((col >= d.cols) || (new_col.size() > d.rows)) {
    std::string why = "Tried to update table column outside table's range";

    if (col >= d.cols)
      why += std::format(", column {} requested when max column is {}", col,
                         d.cols - 1);

    if (new_col.size() > d.rows)
      why += std::format(
          ", new column contains {} rows when table contains at most {} rows",
          new_col.size(), d.rows);

    App::AppContext::Log().warn("Problem encountered updating table -- {}",
                                why);

    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  std::unique_lock lock(_mtx);

  _table.at(col).rows = std::move(new_col);

  _needs_rebuild =
      _table.at(col).recalculate_width(empty_cell) || _needs_rebuild;

  return {};
}

std::expected<void, WidgetErr>
Table::update_col(size_t const col, size_t const row_offset,
                  std::vector<std::optional<UI::String>> &&new_col) noexcept {
  auto const d = *dim.get_const();

  if ((col >= d.cols) || ((new_col.size() + row_offset) > d.rows)) {
    std::string why = "Tried to update table column outside table's range";

    if (col >= d.cols)
      why += std::format(", column {} requested when max column is {}", col,
                         d.cols - 1);

    if ((new_col.size() + row_offset) > d.rows)
      why += std::format(", new column contains {} rows when offset update can "
                         "contain at most",
                         new_col.size(), d.rows - row_offset);

    App::AppContext::Log().warn("Problem encountered updating table -- {}",
                                why);

    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  std::unique_lock lock(_mtx);

  std::move(std::begin(new_col), std::end(new_col),
            std::begin(_table.at(col).rows) + static_cast<long>(row_offset));

  _needs_rebuild =
      _table.at(col).recalculate_width(empty_cell) || _needs_rebuild;

  return {};
}

ThreadSafe::LockPair<Table::Column::Props>
Table::column_props(size_t const col) {
  if (col > _table.size())
    throw std::out_of_range("Column not in range");

  return {std::unique_lock(_mtx), _table.at(col).props};
}

std::expected<void, WidgetErr> Table::clear_at(size_t const row,
                                               size_t const col) noexcept {
  auto const d = *dim.get_const();

  if ((col >= d.cols) || (row >= d.rows))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);
  _table.at(col).rows.at(col) = std::nullopt;

  return {};
}

void Table::update_baseplane(ncpp::Plane *const p) noexcept {
  App::AppContext::Log().debug(
      "Changing table's base plane ({}) to given plane ({})", (size_t)_plane,
      (size_t)p);

  if (_plane == nullptr) [[unlikely]] {
    _plane = p;
  } else {
    _plane->reparent_family(p);
  }
}

std::expected<void, UI::RenderErr> Table::apply(ncpp::Plane *const p) noexcept {
  std::unique_lock lock(_mtx);

  if (_plane != p)
    update_baseplane(p);

  if (_plane == nullptr)
    return UI::RenderErr(
        UI::RenderErr::Source::WIDGET, UI::RenderErr::NULL_PLANE,
        "Tried to apply table when table's internal plane is null");

  if (_needs_rebuild)
    rebuild_columns();

  for (size_t col_idx = 0; col_idx < _table.size(); ++col_idx) {
    auto &col = _table.at(col_idx);
    auto &plane = _col_planes.at(col_idx);

    size_t row_offset = 0;

    for (auto const &row : col.rows) {
      auto row_s = row.value_or(empty_cell);

      size_t x;

      switch (col.props.align) {
      case Align::LEFT:
        x = 0;
        break;
      case Align::CENTER:
        x = (col.max_width - row_s.size()) / 2;
        break;
      case Align::RIGHT:
        x = (col.max_width - row_s.size());
        break;
      }

      row_s.apply_to_plane(&plane, row_offset, x);

      row_offset++;
    }
  }

  return {};
}

std::expected<void, UI::RenderErr>
Table::apply_row(ncpp::Plane *const p, size_t const row_idx) noexcept {
  std::unique_lock lock(_mtx);

  if (_plane != p)
    update_baseplane(p);

  if (_plane == nullptr)
    return UI::RenderErr(
        UI::RenderErr::Source::WIDGET, UI::RenderErr::NULL_PLANE,
        "Tried to apply table row when table's internal plane is null");

  if (_needs_rebuild)
    rebuild_columns();

  for (std::pair<Column &, ncpp::Plane &> col_pair :
       std::ranges::zip_view(_table, _col_planes)) {
    auto &[col, plane] = col_pair;
    auto row_s = col.rows.at(row_idx).value_or(empty_cell);
    size_t x;

    switch (col.props.align) {
    case Align::LEFT:
      x = 0;
      row_s.apply_to_plane(&plane, row_idx, 0, col.max_width);
      break;
    case Align::CENTER:
      x = (col.max_width - row_s.size()) / 2;
      break;
    case Align::RIGHT:
      x = (col.max_width - row_s.size());
      break;
    }

    row_s.apply_to_plane(&plane, row_idx, x, col.max_width);
  }

  return {};
}

void Table::clear() noexcept {
  auto d = *dim.get_const();
  std::unique_lock lock(_mtx);
  _table = std::vector<Column>(d.cols, d.rows);
}
}; // namespace UI
