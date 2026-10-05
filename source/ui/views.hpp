#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <variant>
#include <vector>

// #include "core/input.hpp"
#include "core.hpp"
#include "core/session.hpp"

#include "driver_telemetry.hpp"
#include "google/protobuf/repeated_ptr_field.h"
#include "input.hpp"
#include "locked.hpp"
#include "string.hpp"
#include "telemetry/telemetry_board.hpp"

#include "ui/widget.hpp"

namespace UI::Views {

struct TrackSessionView {
  UI::Table table{};

  static constexpr size_t ROWS = 2;
  static constexpr size_t COLS = 3;

  void update_and_render(Telemetry::TrackSession const &, bool const,
                         std::shared_ptr<ncpp::Plane>);

  bool take_input([[maybe_unused]] Input::KeyWithMod const &k) { return false; }

  TrackSessionView();
};

struct SessionStatusView {
  UI::Table table{};

  static constexpr size_t ROWS = 4;
  static constexpr size_t COLS = 2;

  void update_and_render(Core::Session const &, bool const,
                         std::shared_ptr<ncpp::Plane>);

  bool take_input([[maybe_unused]] Input::KeyWithMod const &k) { return false; }

  SessionStatusView();
};

struct LeaderboardView {
  template <typename Field>
  using ProtoIter = google::protobuf::RepeatedPtrField<Field>;

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
    // requires(std::is_invocable_r_v<UI::String, F,
    //                                Telemetry::DriverTelemetry const &>)
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

  ThreadSafe::Locked<UI::Table> table;

  void set_columns(std::vector<ColumnInfo> &&);
  void set_num_drivers(size_t const);

  void update_and_render(
      ThreadSafe::LockPair<std::vector<Telemetry::DriverTelemetry> const> &&,
      bool const, std::shared_ptr<ncpp::Plane>);

  bool take_input([[maybe_unused]] Input::KeyWithMod const &k) { return false; }

  LeaderboardView();

private:
  ThreadSafe::Locked<std::vector<ColumnInfo>> _cols;
  ThreadSafe::Locked<size_t> _num_drivers;

  void reconstruct_table(ThreadSafe::LockPair<UI::Table> &&,
                         ThreadSafe::LockPair<std::vector<ColumnInfo>> &&,
                         ThreadSafe::LockPair<size_t> &&);
};

using view_variant =
    std::variant<TrackSessionView, SessionStatusView, LeaderboardView>;

struct View : public view_variant {
  using view_variant::variant;
  using view_variant::operator=;

  std::optional<std::function<void(std::shared_ptr<ncpp::Plane>, bool)>>
      render_callback = std::nullopt;

  template <typename F>
    requires(std::is_invocable_r_v<void, F, std::shared_ptr<ncpp::Plane>, bool,
                                   View &>)
  void set_render_callback(F &&f) {
    render_callback = [this, f = std::move(f)](std::shared_ptr<ncpp::Plane> p,
                                               bool focused) {
      f(p, focused, *this);
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

  void take_input(Input::KeyWithMod const &k) {
    this->visit([k](auto &v) { v.take_input(k); });
  }

  template <typename T, typename... Args>
    requires(Core::VariantHasAlternative<T, view_variant> &&
             std::is_constructible_v<T, Args...>)
  View(Args &&...args)
      : view_variant(std::in_place_type<T>, std::forward<Args>(args)...) {}

  // template <typename T>
  //   requires(Core::VariantHasAlternative<T, view_variant>)
  // View() : view_variant(std::in_place_type<T>) {}

  template <typename T, typename... Args>
    requires(Core::VariantHasAlternative<T, view_variant>)
  static std::shared_ptr<View> MakeView(Args &&...args) {
    return std::make_shared<View>(std::in_place_type<T>,
                                  std::forward<Args>(args)...);
  }
};

}; // namespace UI::Views
