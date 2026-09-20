#include <array>
#include <ncpp/Plane.hh>
#include <string>

#include "string.hpp"
#include "telemetry/telemetry_board.hpp"

#include "ui/ui.hpp"
#include "ui/views.hpp"
#include "widget.hpp"

static constexpr std::array<UI::Color, 5> FLAG_TO_COLOR = {
    UI::Color::PURPLE_SOFT, UI::Color::YELLOW_SOFT, UI::Color::GREEN_SOFT,
    UI::Color::RED_SOFT, UI::Color::NONE};

using UI::Table;

namespace UI::Views {

TrackSessionView::TrackSessionView() : table() {
  table.set_dim(Table::Dim(ROWS, COLS));
  table.empty_cell = "";
  table.column_sep = 2u;
}

void TrackSessionView::update_and_render(Telemetry::TrackSession const &sess,
                                         std::shared_ptr<ncpp::Plane> p) {

  UI::String flag =
      UI::String(sess.flag_status.get_const()
                     ->transform(Telemetry::SessionFlag::GetText)
                     .value_or("NONE"),
                 sess.flag_status.get_const()
                     ->transform(Telemetry::SessionFlag::GetStatus)
                     .transform([](Telemetry::FlagStatus f) {
                       return FLAG_TO_COLOR.at(static_cast<size_t>(f));
                     })
                     .value_or(UI::Color::NONE),
                 UI::Style::BOLD);

  std::expected<void, WidgetErr> res;

  res = table.update_row(0, {std::move(flag),
                             sess.event_name.get_const()->value_or("--"),
                             sess.series_name.get_const()->value_or("--")});

  if (sess.session_type.get_const()->value_or(
          Telemetry::SessionType::PRACTICE) == Telemetry::SessionType::RACE)
    // 2nd row is "Laps: <to go> / <total>"
    res = table.update_row(
        1, {"Lap", std::format(
                       "{} / {}",
                       sess.completed_laps.get_const()
                           ->transform([](auto i) { return std::to_string(i); })
                           .value_or("--"),
                       sess.total_laps.get_const()
                           ->transform([](auto i) { return std::to_string(i); })
                           .value_or("--"))});
  else
    res = table.update_row(
        1, {"Time left", sess.time_to_go.get_const()->value_or("--:--:--")});

  table.apply(p);
}

}; // namespace UI::Views
