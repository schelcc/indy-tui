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

struct TableView {
private:
  mutable std::shared_mutex _mtx;

  // IMPROVEMENT : Switch to columns of rows and save on computation of column
  // widths

  // Rows of columns
  std::vector<std::vector<std::optional<UI::String>>> _table{};
  std::vector<size_t> _col_widths{};

public:
  struct Dim {
    size_t rows;
    size_t cols;

    bool operator==(Dim const &o) const {
      return (o.rows == rows) && (o.cols == cols);
    }
  };

  size_t column_sep = 1;

  Dim get_dim() const noexcept;

  void set_dim(Dim const &) noexcept;

  void apply(std::shared_ptr<ncpp::Plane>) const noexcept;

  std::expected<void, ViewErr> set_at(size_t const, size_t const,
                                      UI::String &&) noexcept;

  std::expected<void, ViewErr>
  update_row(size_t const, std::vector<std::optional<UI::String>> &&) noexcept;

  std::expected<void, ViewErr>
  update_row(size_t const, size_t const,
             std::vector<std::optional<UI::String>> &&) noexcept;

  std::expected<void, ViewErr>
  update_col(size_t const, std::vector<std::optional<UI::String>> &&) noexcept;

  std::expected<void, ViewErr>
  update_col(size_t const, size_t const,
             std::vector<std::optional<UI::String>> &&) noexcept;

  std::expected<void, ViewErr> clear_at(size_t const, size_t const) noexcept;
};

}; // namespace UI
