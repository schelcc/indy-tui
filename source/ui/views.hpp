#pragma once

#include <atomic>
#include <cstddef>
#include <expected>
#include <functional>
#include <type_traits>
#include <variant>
#include <vector>

#include "driver_telemetry.hpp"

#include "core/core.hpp"
#include "core/input.hpp"
#include "core/session.hpp"

#include "telemetry/telemetry_board.hpp"

#include "tools/locked.hpp"

#include "ui/string.hpp"
#include "ui/types.hpp"
#include "ui/widget.hpp"

namespace UI::Views {

struct TrackSessionView {
  UI::Table table{};

  static constexpr size_t ROWS = 2;
  static constexpr size_t COLS = 3;

  std::expected<void, RenderErr>
  update_and_render(Telemetry::TrackSession const &, bool const,
                    std::shared_ptr<ncpp::Plane>);

  // Lighter render
  std::expected<void, RenderErr>
  update_and_render(bool const, std::shared_ptr<ncpp::Plane>);

  bool take_input([[maybe_unused]] Input::KeyWithMod const &k) { return false; }

  TrackSessionView();
};

struct SessionStatusView {
  UI::Table table{};

  static constexpr size_t ROWS = 4;
  static constexpr size_t COLS = 2;

  std::expected<void, RenderErr>
  update_and_render(Core::Session const &, bool const,
                    std::shared_ptr<ncpp::Plane>);

  // Lighter render
  std::expected<void, RenderErr>
  update_and_render(bool const, std::shared_ptr<ncpp::Plane>);

  bool take_input([[maybe_unused]] Input::KeyWithMod const &k) { return false; }

  SessionStatusView();
};

struct LeaderboardView {
  using ColumnFunc =
      std::function<UI::String(Telemetry::DriverTelemetry const &)>;

  struct ColumnInfo {
    ColumnFunc func;
    UI::String name;
    UI::Align align;

    template <typename F>
      requires(std::is_invocable_v<F, Telemetry::DriverTelemetry const &> &&
               Core::IsOneOf<
                   std::invoke_result_t<F, Telemetry::DriverTelemetry const &>,
                   UI::String, std::string, std::wstring>)
    ColumnInfo(F &&f, UI::String &&s, UI::Align a = UI::Align::LEFT)
        : func([f = std::forward<F>(f)](Telemetry::DriverTelemetry const &d) {
            return std::invoke(f, d);
          }),
          name(s), align(a) {}

    // TODO - After profile move this to multistr to support returning regular
    // string and wstring
    UI::String operator()(Telemetry::DriverTelemetry const &d) const {
      return std::invoke(func, d);
    }
  };

  struct RowHighlight {
    size_t idx;
    UI::Color color;
  };

  struct TableInfo {
    /// @brief Currently (late 2026) there are 25 charters in the series, so
    /// we'll default to 25 entries in the table. The 500 will be more, and the
    /// charters will increase in '28.
    static constexpr size_t DEFAULT_DRIVER_NUM = 25;

    UI::Table table;
    std::vector<ColumnInfo> cols;
    std::unordered_set<std::string> user_flagged_drivers;

    TableInfo() : table(), cols(), user_flagged_drivers()  {
      table.set_dim(Table::Dim(DEFAULT_DRIVER_NUM, 1));
    }
  };

  ThreadSafe::Locked<TableInfo> table_info;

  std::atomic<size_t> table_cursor = 0;

  Input::InputHandler handler{};

  void set_columns(std::vector<ColumnInfo> &&);

  std::expected<void, RenderErr> update_and_render(
      ThreadSafe::LockPair<std::vector<Telemetry::DriverTelemetry> const> &&,
      bool const, std::shared_ptr<ncpp::Plane>);

  // Lighter render
  std::expected<void, RenderErr>
  update_and_render(bool const, std::shared_ptr<ncpp::Plane>);

  bool take_input(Input::KeyWithMod const &);

  LeaderboardView();

private:
  ThreadSafe::Locked<std::vector<ColumnInfo>> _cols;
  ThreadSafe::Locked<size_t> _num_drivers;

  void reconstruct_table(ThreadSafe::LockPair<TableInfo> &&) noexcept;

  // Logic the same across both renders
  std::expected<void, UI::RenderErr>
  always_render(ThreadSafe::LockPair<TableInfo> &&, bool const,
                std::shared_ptr<ncpp::Plane>);
};

using view_variant =
    std::variant<TrackSessionView, SessionStatusView, LeaderboardView>;

struct View : public view_variant {
  using view_variant::variant;
  using view_variant::operator=;

  using RenderCB = std::function<std::expected<void, RenderErr>(
      UI::RenderLevel, std::shared_ptr<ncpp::Plane>, bool)>;

  std::optional<RenderCB> render_callback = std::nullopt;

  template <typename F>
    requires(std::is_invocable_r_v<std::expected<void, RenderErr>, F,
                                   UI::RenderLevel,
                                   std::shared_ptr<ncpp::Plane>, bool, View &>)
  void set_render_callback(F &&f) {
    render_callback = [this, f = std::move(f)](
                          UI::RenderLevel render_level,
                          std::shared_ptr<ncpp::Plane> p,
                          bool focused) -> std::expected<void, RenderErr> {
      return f(render_level, p, focused, *this);
    };
  }

  template <typename T>
    requires(Core::VariantHasAlternative<T, view_variant>)
  auto &&as(this auto &&self) {
    return std::get<T>(std::forward<decltype(self)>(self));
  }

  template <typename T>
    requires(Core::VariantHasAlternative<T, view_variant>)
  bool is() {
    return std::holds_alternative<T>(*this);
  }

  bool take_input(Input::KeyWithMod const &k) {
    return this->visit([k](auto &v) { return v.take_input(k); });
  }

  template <typename T, typename... Args>
    requires(Core::VariantHasAlternative<T, view_variant> &&
             std::is_constructible_v<T, Args...>)
  View(Args &&...args)
      : view_variant(std::in_place_type<T>, std::forward<Args>(args)...) {}

  template <typename T, typename... Args>
    requires(Core::VariantHasAlternative<T, view_variant>)
  static std::shared_ptr<View> MakeView(Args &&...args) {
    return std::make_shared<View>(std::in_place_type<T>,
                                  std::forward<Args>(args)...);
  }
};

}; // namespace UI::Views
