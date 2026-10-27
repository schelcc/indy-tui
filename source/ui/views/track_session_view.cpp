#include <array>
#include <ncpp/Plane.hh>
#include <string>

#include "tools/draw.hpp"

#include "telemetry/telemetry_board.hpp"

#include "types.hpp"
#include "ui/string.hpp"
#include "ui/views.hpp"
#include "ui/widget.hpp"

static constexpr std::array<UI::Color, 5> FLAG_TO_COLOR = {
    UI::Color::PURPLE_SOFT, UI::Color::YELLOW_SOFT, UI::Color::GREEN_SOFT,
    UI::Color::RED_SOFT, UI::Color::NONE};

using UI::Table;

namespace UI::Views {

TrackSessionView::TrackSessionView() : table("track-session") {
  table.set_dim(Table::Dim(ROWS, COLS));
  table.empty_cell = "";
  table.column_sep = 2u;
  table.column_props(0)->align = Align::RIGHT;
}

std::expected<void, UI::RenderErr>
TrackSessionView::update_and_render(Telemetry::TrackSession const &sess,
                                    FocusState const focused,
                                    ncpp::Plane *const p) noexcept {

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

  if (!res.has_value())
    return RenderErr(UI::RenderErr::Source::VIEW, UI::RenderErr::TABLE_ERR,
                     "Error updating event and series name -- " +
                         res.error().msg.value_or("no msg"));

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

  if (!res.has_value())
    return RenderErr(UI::RenderErr::Source::VIEW, UI::RenderErr::TABLE_ERR,
                     "Error updating lap/time-left -- " +
                         res.error().msg.value_or("no msg"));

  TRY_DROP(table.apply(p));
  if (_prev_focus != focused)
    Tools::Draw::border_with_title(
        p, "Event Status", focused ? UI::Color::CYAN_SOFT : UI::Color::NONE);

  _prev_focus = focused;

  return {};
}

std::expected<void, UI::RenderErr>
TrackSessionView::update_and_render(FocusState const focused,
                                    ncpp::Plane *const p) noexcept {
  TRY_DROP(table.apply(p));

  if (_prev_focus != focused)
    Tools::Draw::border_with_title(
        p, "Event Status", focused ? UI::Color::CYAN_SOFT : UI::Color::NONE);

  _prev_focus = focused;

  return {};
}

}; // namespace UI::Views
