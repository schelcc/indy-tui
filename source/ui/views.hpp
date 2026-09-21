#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>
#include <vector>

// #include "core/input.hpp"
#include "core.hpp"
#include "core/session.hpp"

#include "driver_telemetry.hpp"
#include "google/protobuf/repeated_ptr_field.h"
#include "locked.hpp"
#include "string.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_queue.hpp"

#include "ui/widget.hpp"

namespace UI::Views {

struct TrackSessionView {
  UI::Table table{};

  static constexpr size_t ROWS = 2;
  static constexpr size_t COLS = 3;

  void update_and_render(Telemetry::TrackSession const &,
                         std::shared_ptr<ncpp::Plane>);

  TrackSessionView();
};

struct SessionStatusView {
  UI::Table table{};

  static constexpr size_t ROWS = 4;
  static constexpr size_t COLS = 2;

  void update_and_render(Core::Session const &, std::shared_ptr<ncpp::Plane>);

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
      ThreadSafe::LockPair<std::vector<Telemetry::DriverTelemetry> const &> &&,
      std::shared_ptr<ncpp::Plane>);

  LeaderboardView();

private:
  ThreadSafe::Locked<std::vector<ColumnInfo>> _cols;
  ThreadSafe::Locked<size_t> _num_drivers;

  void reconstruct_table(ThreadSafe::LockPair<UI::Table &> &&,
                         ThreadSafe::LockPair<std::vector<ColumnInfo> &> &&,
                         ThreadSafe::LockPair<size_t &> &&);
};

}; // namespace UI::Views
