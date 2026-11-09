#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <expected>
#include <iterator>
#include <mutex>

#include <ncpp/NCBox.hh>
#include <ncpp/Plane.hh>
#include <notcurses/notcurses.h>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "app_context.hpp"
#include "draw.hpp"
#include "tools/locked.hpp"

#include "types.hpp"
#include "ui.hpp"
#include "ui/string.hpp"
#include "ui/widget.hpp"

namespace UI {

/* Local-only helpers */
inline std::expected<void, UI::RenderErr>
draw_row(bool const drawing_border, Table::Column const &col,
         ncpp::Plane &plane, std::optional<UI::String> const &row,
         size_t const row_offset, UI::String const &empty_cell) noexcept {
  auto row_s = row.value_or(empty_cell);

  size_t x = 0;

  switch (col.props.align) {
  case Align::LEFT:
    break;
  case Align::CENTER:
    x = (col.max_width - row_s.size()) / 2;
    break;
  case Align::RIGHT:
    x = (col.max_width - row_s.size());
    break;
  }

  x += drawing_border;

  // Draw spaces before cell text
  if (x > 0)
    plane.putstr(row_offset, drawing_border,
                 std::string(x - drawing_border, ' ').c_str());

  return row_s.apply_to_plane(&plane, row_offset, x,
                              col.max_width + drawing_border);
}

/* Column methods */
bool Table::Column::recalculate_width(UI::String const &default_str) {

  auto pre_width = max_width;

  /*
  NOTE : I'm not sure why, but we have to add 1 to the retrieved size
  for these recalculate calls. Maybe later problem
  */

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

/* Table private methods */
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

void Table::rebuild_columns() noexcept {
  App::AppContext::Log().debug("Rebuilding table columns for {}", name);

  _col_planes.clear();
  _col_planes.reserve(_table.size());

  bool const drawing_border = draw_border.test();

  // If drawing the border, should add 2 to column widths to account
  // for drawn border symbols
  size_t const cell_padding = drawing_border * 2;

  // How much to always step x_off forward each iter. We want border
  // edges to overlap, so for border additions we'll add just 1 or 0
  // accordingly
  size_t const width_adj = drawing_border + column_sep.load();

  size_t const y_off = 1 + padding_top;

  // Ensure ylen is exactly the length the table needs, and no more
  size_t const ylen = separator_rows.get_const()->size() +
                      dim.get_const()->rows + (drawing_border * 2);

  size_t x_off = 1 + padding_left;

  for (auto &col : _table) {
    // Necessary in case first rebuild beats an update call
    col.recalculate_width(empty_cell.size());

    _col_planes.emplace_back(_plane, ylen, col.max_width + cell_padding, y_off,
                             x_off);
    x_off += col.max_width + width_adj;

    // If not marked as damaged, a rarely-updating column is not
    // redrawn after it was cleared and just disappears
    col.damaged = true;
  }

  _col_planes.shrink_to_fit();

  // Always force a remapping of cursor rows after rebuilds
  _table_row_ypos_map.clear();

  // Always force a remapping of sep rows after rebuilds
  _repop_sep_row_trans = true;
  _sep_rows_trans.clear();

  _needs_rebuild = false;
}

std::expected<void, UI::RenderErr>
Table::redraw_cursor(size_t const cursor_index,
                     std::vector<size_t> const &redraw_indices,
                     bool const cursor_moved, bool const drawing_border,
                     std::unordered_set<size_t> &&sep_rows) noexcept {
  size_t cursor_ypos = cursor_index;

  size_t last_cursor_index = _last_cursor_pos;
  size_t last_cursor_ypos = _last_cursor_pos;

  _last_cursor_pos = cursor_index;

  // If we're drawing the border there's a likelihood our index
  // doesn't match our ypos, so use the mapping to go from index to
  // plane ypos
  if (draw_border.test()) {
    if (_table_row_ypos_map.empty()) {
      size_t ypos = drawing_border;
      size_t row_idx = 0;

      auto row_num = dim.get_const()->rows;
      while (row_idx < row_num) {
        _table_row_ypos_map.emplace_back(ypos);
        ypos += (1 + sep_rows.contains(row_idx++));
      }
    }

    cursor_ypos = _table_row_ypos_map.at(cursor_index);
    last_cursor_ypos = _table_row_ypos_map.at(last_cursor_index);
  }

  auto color = Tools::Draw::mask_pal_channels(_plane, Tools::Draw::Target::BG,
                                              UI::Color::BLUE_SOFT);

  // If the cursor moved, we need to draw all columns, otherwise we
  // just need to redraw any columns that were redrawn earlier
  for (auto col_idx : redraw_indices) {
    auto &col = _table.at(col_idx);
    auto &plane = _col_planes.at(col_idx);

    if (cursor_moved) {
      // Re-draw prior row
      TRY_DROP(draw_row(drawing_border, col, plane,
                        col.rows.at(last_cursor_index), last_cursor_ypos,
                        empty_cell));

      // If drawing border, Replace the the previous row's border
      // cells with the current row's border cells to erase prior
      // cursor stain w/o redrawing all borders
      if (draw_border.test()) {
        ncpp::Cell base_cell;

        plane.get_at(cursor_ypos, 0, base_cell);
        plane.putc(last_cursor_ypos, 0, base_cell);

        plane.get_at(cursor_ypos, plane.get_dim_x() - 1, base_cell);
        plane.putc(last_cursor_ypos, plane.get_dim_x() - 1, base_cell);
      }
    }

    plane.stain(cursor_ypos, 0, 1, plane.get_dim_x(), color, color, color,
                color);
  }

  return {};
}

/* Table public methods */
size_t Table::move_cursor(UI::CursorDirection const d) noexcept {
  if (!draw_cursor.test()) {
    App::AppContext::Log().warn(
        "Tried to move cursor on plane with cursor drawing disabled");
    return 0;
  }

  auto skip_rows = cursor_skip_rows.get_const();

  auto cursor = _cursor_pos.load();
  auto cursor_target = cursor;
  auto table_len = dim.get_const()->rows;

  do {
    if (d == UI::CursorDirection::NEXT) {
      cursor_target = ((cursor_target + 1) < table_len) ? cursor_target + 1 : 0;
    } else {
      cursor_target = (cursor_target > 0) ? cursor_target - 1 : table_len - 1;
    }
  } while (skip_rows->contains(cursor_target));

  while (!_cursor_pos.compare_exchange_strong(cursor, cursor_target)) {
    cursor_target = cursor;
    do {
      if (d == UI::CursorDirection::NEXT) {
        cursor_target =
            ((cursor_target + 1) < table_len) ? cursor_target + 1 : 0;
      } else {
        cursor_target = (cursor_target > 0) ? cursor_target - 1 : table_len - 1;
      }
    } while (skip_rows->contains(cursor_target));
  }

  App::AppContext::Log().trace("Cursor position is now: {}", cursor_target);

  return cursor;
}

std::string Table::cursor_backing_key() const noexcept {
  // There's 100% a better way to do this
  size_t const cursor = _cursor_pos.load();
  size_t const adj = std::ranges::fold_left(
      *cursor_skip_rows.get_const(), 0,
      [cursor](size_t &&i, size_t r) -> size_t { return i + (r < cursor); });

  return (draw_cursor.test() && !no_backing_keys.test())
             ? _backing_keys.at(cursor - adj)
             : "";
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

  // Generate a vec of indices so we can avoid doing so repetitively later
  _col_range = std::views::iota(0) | std::views::take(_table.size()) |
               std::ranges::to<std::vector<size_t>>();

  *old_dim = d;

  if (!no_backing_keys.test())
    _backing_keys = std::vector<std::string>(d.rows, "");

  return;
}

ThreadSafe::LockPair<Table::Column::Props>
Table::column_props(size_t const col) {
  if (col > _table.size())
    throw std::out_of_range("Column not in range");

  return {std::unique_lock(_mtx), _table.at(col).props};
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

void Table::damage_all() noexcept {
  std::unique_lock lock(_mtx);
  for (auto &c : _table)
    c.damaged = true;
}

std::expected<void, UI::RenderErr> Table::apply(ncpp::Plane *const p) noexcept {
  std::unique_lock lock(_mtx);

  if (_plane != p)
    update_baseplane(p);

  if (_plane == nullptr)
    return UI::RenderErr(
        UI::RenderErr::Source::WIDGET, UI::RenderErr::NULL_PLANE,
        "Tried to apply table when table's internal plane is null");

  const bool drawing_cursor = draw_cursor.test();
  const bool drawing_border = draw_border.test();

  const bool to_redraw_border = drawing_border && _needs_rebuild;

  if (_needs_rebuild) {
    rebuild_columns();
  }

  App::AppContext::Log().trace("Draw table {}", name);

  // Group columns, their planes, and their index, then filter out any
  // columns which are not damaged
  auto v = std::views::zip(_table, _col_planes, std::views::iota(0)) |
           std::views::filter(
               [](auto const &p) -> bool { return std::get<0>(p).damaged; });

  std::unordered_set<size_t> sep_rows = *separator_rows.get_const();

  // Track which columns we've updated so that we can limit cursor
  // drawing to them, if drawing cursor
  std::vector<size_t> to_redraw_cursor{};

  if (drawing_cursor)
    to_redraw_cursor.reserve(_table.size());

  for (auto [col, plane, idx] : v) {
    size_t row_idx = 0;
    size_t row_offset = drawing_border;

    for (auto const &row : col.rows) {
      TRY_DROP(
          draw_row(drawing_border, col, plane, row, row_offset, empty_cell));

      const bool is_sep = sep_rows.contains(row_idx++);

      if (_repop_sep_row_trans && is_sep) {

        int col_x = 0;
        int col_y = row_offset + 1;

        // Translate this row's ypos from the column plane to the table's plane
        ncplane_translate(plane.to_ncplane(), _plane->to_ncplane(), &col_y,
                          &col_x);

        App::AppContext::Log().debug(
            "Translated sep. row {} to base plane row {}", row_idx - 1, col_y);

        _sep_rows_trans.emplace_back(col_y);
      }

      row_offset += (1 + is_sep);
    }

    _repop_sep_row_trans = false;

    if (drawing_cursor)
      to_redraw_cursor.emplace_back(idx);

    col.damaged = false;
  }

  // Draw cursor
  size_t cursor_index = _cursor_pos.load();
  const bool cursor_moved = cursor_index != _last_cursor_pos;

  // Only draw/redraw cursor if it has moved, or if some number of
  // columns were redrawn earlier
  if (drawing_cursor && (cursor_moved || !to_redraw_cursor.empty())) {
    // Handle for cursor starting on a skip row
    if (cursor_skip_rows.get_const()->contains(cursor_index))
      cursor_index = move_cursor(CursorDirection::NEXT);

    auto &redraw_indices = cursor_moved ? _col_range : to_redraw_cursor;
    TRY_DROP(redraw_cursor(cursor_index, redraw_indices, cursor_moved,
                           drawing_border, std::move(sep_rows)));
  }

  // If we're not drawing the cursor but just last render were, redraw
  // the border to erase the border's cursor stain
  if (to_redraw_border || (!drawing_cursor && _last_draw_cursor))
    redraw_border();

  _last_draw_cursor = drawing_cursor;

  return {};
}

void Table::redraw_border() noexcept {
  auto color = Tools::Draw::mask_pal_channels(_plane, Tools::Draw::Target::FG,
                                              UI::Color::NONE);

  namespace BOXDRAW_SET = Tools::Draw::BOXDRAW_ROUND;

  _plane->erase();

  std::wstring sep_str = L"";

  for (auto [col, plane, idx] :
       std::ranges::zip_view{_table, _col_planes, std::views::iota(0u)}) {
    auto border_mask = ncpp::NCBox::CornerMask;

    border_mask |= (!col.props.draw_left_border * ncpp::NCBox::MaskLeft);

    plane.putwch(0, 0,
                 col.props.draw_left_border ? BOXDRAW_SET::T_D
                                            : BOXDRAW_SET::HORI);
    plane.putwch(plane.get_dim_y() - 1, 0,
                 col.props.draw_left_border ? BOXDRAW_SET::T_U
                                            : BOXDRAW_SET::HORI);

    // If not drawing a left-border, draw a space char there
    if (!col.props.draw_left_border) {
      ncpp::Cell c(' ');
      plane.cursor_move(1, 0);
      plane.vline(c, plane.get_dim_y() - 2);
    }

    plane.perimeter_rounded(0, color, border_mask);

    for (int const row : _sep_rows_trans) {
      int y = row;
      int x = 0;
      ncplane_translate(_plane->to_ncplane(), plane.to_ncplane(), &y, &x);

      // Erase the row
      ncplane_erase_region(plane.to_ncplane(), y, 0, 1, 0);
    }

    // Add this column's left border divider char to the separator str
    if (idx == 0)
      sep_str += BOXDRAW_SET::T_R;
    else if (idx == _table.size())
      sep_str += BOXDRAW_SET::T_L;
    else
      sep_str +=
          col.props.draw_left_border ? BOXDRAW_SET::CROSS : BOXDRAW_SET::HORI;

    // Add horizontal chars to the width of this column to the separator str
    sep_str += std::wstring(col.max_width, BOXDRAW_SET::HORI);
  }

  // Add the final border divider char to the separator str
  sep_str += BOXDRAW_SET::T_L;

  auto &leftmost = _col_planes.front();
  leftmost.putwch(0, 0, BOXDRAW_SET::UL);
  leftmost.putwch(leftmost.get_dim_y() - 1, 0, BOXDRAW_SET::LL);

  auto &rightmost = _col_planes.back();
  auto xpos = rightmost.get_dim_x() - 1;
  rightmost.putwch(0, xpos, BOXDRAW_SET::UR);
  rightmost.putwch(rightmost.get_dim_y() - 1, xpos, BOXDRAW_SET::LR);

  size_t left_pad = padding_left + 1;

  for (size_t const row : _sep_rows_trans) {
    _plane->putstr(row, left_pad, sep_str.c_str());
  }
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

  auto &target_col = _table.at(col);
  auto &target_row = target_col.rows.at(row);

  target_col.damaged = target_col.damaged || !target_row.has_value() ||
                       (target_row.value() != str);

  target_row = std::move(str);

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
    auto &target_row = target_col.rows.at(row);

    _needs_rebuild =
        target_col.recalculate_width(s.value_or(empty_cell)) || _needs_rebuild;

    target_col.damaged = target_col.damaged || (target_row != s);

    target_row = std::move(s);

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
    auto &target_row = target_col.rows.at(row);

    _needs_rebuild =
        target_col.recalculate_width(s.value_or(empty_cell)) || _needs_rebuild;

    target_col.damaged = target_col.damaged || (target_row != s);

    target_row = std::move(s);

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

  auto &target_col = _table.at(col);

  auto new_it = new_col.cbegin();
  auto old_it = target_col.rows.cbegin();

  while (!target_col.damaged && (new_it < new_col.cend()) &&
         (old_it < target_col.rows.cend()))
    target_col.damaged = *(new_it++) == *(old_it++);

  target_col.rows = std::move(new_col);

  _needs_rebuild = target_col.recalculate_width(empty_cell) || _needs_rebuild;

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

  auto &target_col = _table.at(col);

  // target_col.damaged =
  //     target_col.damaged ||
  //     std::ranges::equal(target_col.rows | std::views::take(new_col.size()) |
  //                            std::views::drop(row_offset),
  //                        new_col);
  auto new_it = new_col.cbegin();
  auto old_it = target_col.rows.cbegin();

  while (!target_col.damaged && (new_it < new_col.cend()) &&
         (old_it < target_col.rows.cend()))
    target_col.damaged = *(new_it++) == *(old_it++);

  std::move(std::begin(new_col), std::end(new_col),
            std::begin(_table.at(col).rows) + static_cast<long>(row_offset));

  _needs_rebuild =
      _table.at(col).recalculate_width(empty_cell) || _needs_rebuild;

  return {};
}

std::expected<void, WidgetErr> Table::swp_update_col(
    size_t const col,
    std::vector<std::optional<UI::String>> &swp_col) noexcept {
  auto const d = *dim.get_const();

  if ((col >= d.cols) || (swp_col.size() != d.rows)) {
    std::string why = "Tried to swap invalid table column";

    if (col >= d.cols)
      why += std::format(", column {} requested when max column is {}", col,
                         d.cols - 1);

    if (swp_col.size() != d.rows)
      why += std::format(
          ", column to swap with contains {} rows when table contains {} rows",
          swp_col.size(), d.rows);

    App::AppContext::Log().warn("Problem encountered updating table -- {}",
                                why);

    return WidgetErr(WidgetErr::OUT_OF_RANGE);
  }

  std::unique_lock lock(_mtx);

  auto &target_col = _table.at(col);

  auto new_it = swp_col.cbegin();
  auto old_it = target_col.rows.cbegin();

  while (!target_col.damaged && (new_it < swp_col.cend()) &&
         (old_it < target_col.rows.cend()))
    target_col.damaged = *(new_it++) != *(old_it++);

  target_col.rows.swap(swp_col);

  _needs_rebuild = target_col.recalculate_width(empty_cell) || _needs_rebuild;

  return {};
}

std::expected<void, WidgetErr> Table::clear_at(size_t const row,
                                               size_t const col) noexcept {
  auto const d = *dim.get_const();

  if ((col >= d.cols) || (row >= d.rows))
    return WidgetErr(WidgetErr::OUT_OF_RANGE);

  std::unique_lock lock(_mtx);
  auto &target_col = _table.at(col);
  auto &target_row = target_col.rows.at(row);

  target_col.damaged = target_col.damaged || target_row.has_value();
  target_row = std::nullopt;

  return {};
}

void Table::clear() noexcept {
  auto d = *dim.get_const();
  std::unique_lock lock(_mtx);
  _table = std::vector<Column>(d.cols, d.rows);
}
}; // namespace UI
