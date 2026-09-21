
#include <ncpp/Plane.hh>

#include "core/session.hpp"

#include "ui/views.hpp"
#include "ui/widget.hpp"

using UI::Table;

namespace UI::Views {

SessionStatusView::SessionStatusView() : table() {
  table.set_dim(Table::Dim(ROWS, COLS));
  table.column_sep = 2;
  table.empty_cell = "";
  auto res = table.update_col(0, {"Connection Status", "Message Rate",
                                  "Configured Delay", "Delay Amount"});
  table.column_props(0)->align = Align::RIGHT;
}

void SessionStatusView::update_and_render(Core::Session const &sess,
                                          std::shared_ptr<ncpp::Plane> p) {
  UI::String delay_s = "--";
  auto delay_res = sess.get_delay_sec();
  if (delay_res.has_value())
    delay_s = std::format("{} s", delay_res.value());

  double msg_rate = 1000 / sess.msg_recv_period.load().count();
  UI::Color msg_rate_color;
  if (msg_rate >= 9.0)
    msg_rate_color = UI::Color::GREEN_SOFT;
  else if (msg_rate >= 8.0)
    msg_rate_color = UI::Color::YELLOW_SOFT;
  else
    msg_rate_color = UI::Color::RED_SOFT;

  UI::String msg_rate_str(std::format("{:5.2f} Hz", msg_rate), msg_rate_color);
  Time::Duration::DblMilliSec since_last_msg =
      Time::Clock::now() - sess.last_msg_time.load();

  if (since_last_msg.count() > 200)
    msg_rate_str += UI::String(std::format(" (last msg. received {:.2f}s ago)",
                                           since_last_msg.count() / 1000),
                               UI::Color::GRAY_HARD, UI::Style::ITALIC);

  double delay_pct =
      sess.get_accrued_delay_ms().count() / (1000 * delay_res.value_or(1));

  auto res = table.update_col(
      1, {Core::SessionStatusStrs.at(static_cast<size_t>(sess.status.load())),
          std::move(msg_rate_str), std::move(delay_s),
          std::format("{:5.2f}%", delay_pct * 100)});

  if (res.has_value())
    table.apply(p);
}

}; // namespace UI::Views
