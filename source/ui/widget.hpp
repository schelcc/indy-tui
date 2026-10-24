#pragma once

#include <atomic>
#include <cstddef>
#include <execution>
#include <expected>
#include <functional>
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <ncpp/Plane.hh>

#include "core/core.hpp"
#include "core/input.hpp"

#include "tools/locked.hpp"

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

template <typename V>
concept WidgetLike = requires(V v, Input::KeyWithMod &k) {
  // Must take in input, return bool denoting whether it used
  // the input
  { v.take_input(k) } -> std::same_as<bool>;
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
  std::atomic_uint16_t padding_left = 0;

  /// @brief How far from the area's top edge should be top untouched, defaults
  /// 0
  std::atomic_uint16_t padding_top = 0;

  /// @brief How far from the area's right edge should be right untouched,
  /// defaults 0
  std::atomic_uint16_t padding_right = 0;

  /// @brief How far from the area's bottom edge should be bottom untouched,
  /// defaults 0
  std::atomic_uint16_t padding_bottom = 0;

  bool take_input(Input::KeyWithMod &);

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

  // Columns of rows
  std::vector<Column> _table{};

  // Coooooould use std::any here and require caller to provide the type when
  // resolving backing keys, but seems like overkill

  /// @brief Keys to associate with rows that are never shown upon
  /// render, are only used to provide the user a way to associate row
  /// content. Kept at the same dimensions as the table's rows unless
  /// `no_backing_keys` is true.
  std::vector<std::string> _backing_keys{};

public:
  // Properties

  /// @brief Spacing between columns, defaults to 1
  std::atomic_uint8_t column_sep = 1;

  /// @brief Spacing between rows, defaults to 0
  std::atomic_uint8_t row_sep = 1;

  /// @brief Empty cell representation, defaults to "--"
  UI::String empty_cell = "--";

  /// @brief Whether to disable backing keys. To attempt to retrieve
  /// backing keys when this is set is an error.
  std::atomic_flag no_backing_keys = true;

  // Methods
  bool take_input(Input::KeyWithMod &);

  /** @brief Retrieve the table's current dimensions. */
  Dim get_dim() const noexcept;
  std::pair<std::unique_lock<std::shared_mutex>, Dim>
  get_dim(std::unique_lock<std::shared_mutex> &&) const noexcept;

  /** @brief Set the table's dimensions. Fully clears the table. */
  void set_dim(Dim const &) noexcept;

  /** @brief Access a column's properties. Throws if column is not within range.
   */
  ThreadSafe::LockPair<Column::Props> column_props(size_t const);

  /** @brief Retrieve the backing key associated with the given row. Requires
   * `no_backing_keys` not be set. */
  std::expected<std::string, WidgetErr>
  get_backing_key(size_t const) const noexcept;

  /** @brief Retrieve a collection of row indices associated with the given
   * keys. If no keys match, returns empty vector. Requires `no_backing_keys`
   * not be set. */
  std::expected<std::vector<size_t>, WidgetErr>
  batch_key_lookup(std::unordered_set<std::string> const &) const noexcept;

  /** @brief Get the row index associated with the given backing key,
   * if one exists. Returns WidgetErr::OUT_OF_RANGE if no such key
   * exists in the table. Requires `no_backing_keys` not be set. */
  std::expected<size_t, WidgetErr>
  get_backing_key_row(std::string_view const) const noexcept;

  /** @brief Set the backing key associated with the given row. Requires
   * `no_backing_keys` not be set. */
  std::expected<void, WidgetErr>
  set_backing_key(size_t const, std::string_view const) noexcept;

  /** @brief Set the backing keys for the full table, optionally providing an
   * offset to place the start of the new keys. Requires `no_backing_keys` not
   * be set. */
  std::expected<void, WidgetErr> set_backing_keys(std::vector<std::string> &&,
                                                  size_t const = 0) noexcept;

  /** @brief Apply this view to a plane. Does not synchronize plane access. */
  void apply(std::shared_ptr<ncpp::Plane>) const noexcept;

  /** @brief Apply select rows to a plane. Does not synchronize plane access. */
  void apply_row(std::shared_ptr<ncpp::Plane>, size_t const) const noexcept;

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

template <typename... Ts>
  requires(WidgetLike<Ts> && ...)
struct _Widget : std::variant<Ts...> {
  using std::variant<Ts...>::variant;
  using std::variant<Ts...>::operator=;

  std::optional<std::function<void(std::shared_ptr<ncpp::Plane>)>>
      render_callback = std::nullopt;

  template <typename T, typename Self>
    requires(Core::IsOneOf_ignore_cvref<T, Ts...>)
  auto &&as(this Self &&self) {
    return std::get<T>(std::forward<Self>(self));
  }

  template <typename T>
    requires(Core::IsOneOf_ignore_cvref<T, Ts...>)
  bool is() {
    return std::holds_alternative<T>(*this);
  }

  template <typename T>
    requires(Core::IsOneOf_ignore_cvref<T, Ts...>)
  _Widget(T &&t) : std::variant<Ts...>::variant(std::forward<T>(t)) {}

  template <Core::IsOneOf<Ts...> T, typename... Args>
  static std::shared_ptr<_Widget> MakeWidget(Args &&...args) {
    return std::make_shared<_Widget>(std::in_place_type<T>,
                                     std::forward<Args>(args)...);
  }

  template <typename F>
    requires(std::is_invocable_r_v<void, F, std::shared_ptr<ncpp::Plane>>)
  void set_render_callback(F &&f) {
    render_callback = [f = std::move(f)](std::shared_ptr<ncpp::Plane> p) {
      f(p);
    };
  }
};

using Widget = _Widget<Text, Table>;

}; // namespace UI
