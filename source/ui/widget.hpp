#pragma once

#include <cstddef>
#include <execution>
#include <expected>
#include <memory>
#include <mutex>
#include <ncpp/Plane.hh>
#include <shared_mutex>
#include <vector>

#include "core/input.hpp"
#include "string.hpp"
#include "tools/draw.hpp"
#include "ui/ui.hpp"

namespace UI {

enum class LayoutDirection { HORIZONTAL, VERTICAL };
enum class FocusDir { NEXT, PREV };

using Segments = size_t;

struct ViewErr {
  enum Kind {
    OUT_OF_RANGE,
  } kind;

  std::optional<std::string> msg = {};

  // Implicit conversion from a ViewErr to std::unexpected(ViewErr) for ease of
  // use
  template <typename S> operator std::expected<S, ViewErr>() {
    return std::unexpected(*this);
  }

  ViewErr(Kind k) : kind(k) {}
  ViewErr(Kind k, std::string_view m) : kind(k), msg(m) {}
};

// Views
struct TextView {
  std::variant<std::string, std::wstring, UI::String> text{};
};

/** @brief Interface view for table-based information displays. */
struct TableView {
private:
  mutable std::shared_mutex _mtx;

  // IMPROVEMENT : Switch to columns of rows and save on computation of column
  // widths

  // Rows of columns
  std::vector<std::vector<std::optional<UI::String>>> _table{};
  std::vector<size_t> _col_widths{};

public:
  /** @brief Dataclass to store table's dimensions. */
  struct Dim {
    size_t rows;
    size_t cols;

    bool operator==(Dim const &o) const {
      return (o.rows == rows) && (o.cols == cols);
    }
  };

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

  /** @brief Apply this view to a plane. Does not synchronize plane access. */
  void apply(std::shared_ptr<ncpp::Plane>) const noexcept;

  /** @brief Set the string at the specified row and column to the given value.
   * Cell must be within the table's current dimensions. Returns expected
   * containing void if successful, otherwise the error which occured. */
  std::expected<void, ViewErr> set_at(size_t const, size_t const,
                                      UI::String &&) noexcept;

  /** @brief Starting at the zeroth column in the given row,
   * sequentially update the row's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, ViewErr>
  update_row(size_t const, std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Starting at the specified column in the given row,
   * sequentially update the row's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, ViewErr>
  update_row(size_t const, size_t const,
             std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Starting at the zeroth row in the given column,
   * sequentially update the column's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, ViewErr>
  update_col(size_t const, std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Starting at the specified row in the given column,
   * sequentially update the column's cells with the given values. All
   * cells must fit within the table's current dimensions.  Returns
   * expected containing void if successful, otherwise the error which
   * occured. */
  std::expected<void, ViewErr>
  update_col(size_t const, size_t const,
             std::vector<std::optional<UI::String>> &&) noexcept;

  /** @brief Set the specified cell to empty. */
  std::expected<void, ViewErr> clear_at(size_t const, size_t const) noexcept;

  /** @brief Set all cells to empty. Maintains current dimensions. */
  void clear() noexcept;
};

}; // namespace UI
