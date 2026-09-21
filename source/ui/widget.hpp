#pragma once

#include <cstddef>
#include <execution>
#include <expected>
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <vector>

#include <ncpp/Plane.hh>

#include "locked.hpp"
#include "ui/string.hpp"

namespace UI {

enum class Align { LEFT, CENTER, RIGHT };

struct WidgetErr {
  enum Kind {
    OUT_OF_RANGE,
    ACTION_DISABLED,
  } kind;

  std::optional<std::string> msg = {};

  // Implicit conversion from a WidgetErr to std::unexpected(WidgetErr) for ease
  // of use
  template <typename S> operator std::expected<S, WidgetErr>() {
    return std::unexpected(*this);
  }

  WidgetErr(Kind k) : kind(k) {}
  WidgetErr(Kind k, std::string_view m) : kind(k), msg(m) {}
};

// Widgets
struct Text {
private:
  std::shared_mutex _mtx;

  /// @brief Text to be rendered.
  UI::String text{};

public:
  /// @brief Alignment of text when rendered, defaults to left
  std::atomic<Align> align = Align::LEFT;

  // TODO - Line wrapping and padding behavior
  /// @brief How far from the area's left edge should be left untouched,
  /// defaults 0
  std::atomic_size_t padding_left = 0;

  /// @brief How far from the area's top edge should be top untouched, defaults
  /// 0
  std::atomic_size_t padding_top = 0;

  /// @brief How far from the area's right edge should be right untouched,
  /// defaults 0
  std::atomic_size_t padding_right = 0;

  /// @brief How far from the area's bottom edge should be bottom untouched,
  /// defaults 0
  std::atomic_size_t padding_bottom = 0;

  /** @brief Append the given text to the existing text. */
  void append(UI::String &&) noexcept;

  /** @brief Replace all existing text with the given text. */
  void update(UI::String &&) noexcept;

  /** @brief Clear the existing text. */
  void clear() noexcept;

  /** @brief Apply the existing text onto the given plane. Does not synchronize
   * plane access. */
  void apply(std::shared_ptr<ncpp::Plane>) noexcept;
};

/** @brief Interface widget for table-based information displays. */
struct Table {
public:
  /** @brief Dataclass to store table's dimensions. */
  struct Dim {
    size_t rows;
    size_t cols;

    bool operator==(Dim const &o) const {
      return (o.rows == rows) && (o.cols == cols);
    }
  };

  /** @brief Dataclass to store column-based formatting properties. */
  struct Column {
    std::vector<std::optional<UI::String>> rows;
    size_t max_width = 0;

    struct Props {
      Align align = Align::LEFT;
    } props{};

    /** @brief Recalculate this column's max_width. */
    void recalculate_width(UI::String const &);
  };

private:
  mutable std::shared_mutex _mtx;

  // IMPROVEMENT : Switch to columns of rows and save on computation of column
  // widths

  // Columns of rows
  // std::vector<std::vector<std::optional<UI::String>>> _table{};
  std::vector<Column> _table{};

public:
  // Properties

  /// @brief Spacing between columns, defaults to 1
  std::atomic_size_t column_sep = 1;

  /// @brief Spacing between rows, defaults to 0
  std::atomic_size_t row_sep = 1;

  /// @brief Empty cell representation, defaults to "--"
  UI::String empty_cell = "--";

  // Methods
  /** @brief Retrieve the table's current dimensions. */
  Dim get_dim() const noexcept;

  /** @brief Set the table's dimensions. Fully clears the table. */
  void set_dim(Dim const &) noexcept;

  /** @brief Access a column's properties. Throws if column is not within range.
   */
  ThreadSafe::LockPair<Column::Props &> column_props(size_t const);

  /** @brief Apply this view to a plane. Does not synchronize plane access. */
  void apply(std::shared_ptr<ncpp::Plane>) const noexcept;

  /** @brief Set the string at the specified row and column to the given value.
   * Cell must be within the table's current dimensions. Returns expected
   * containing void if successful, otherwise the error which occured. */
  std::expected<void, WidgetErr> set_at(size_t const, size_t const,
                                        UI::String &&) noexcept;

  /** @brief Starting at the zeroth column in the given row,
   * sequentially update the row's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, WidgetErr>
  update_row(size_t const, std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Starting at the specified column in the given row,
   * sequentially update the row's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, WidgetErr>
  update_row(size_t const, size_t const,
             std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Starting at the zeroth row in the given column,
   * sequentially update the column's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, WidgetErr>
  update_col(size_t const, std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Starting at the specified row in the given column,
   * sequentially update the column's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, WidgetErr>
  update_col(size_t const, size_t const,
             std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Set the specified cell to empty. */
  std::expected<void, WidgetErr> clear_at(size_t const, size_t const) noexcept;

  /** @brief Set all cells to empty. Maintains current dimensions. */
  void clear() noexcept;

  Table() = default;
};

}; // namespace UI
