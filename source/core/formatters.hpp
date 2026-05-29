#include <format>
#include <iomanip>
#include <sstream>
#include <telemetry/telemetry_queue.hpp>

template <> struct std::formatter<Telemetry::TelemetryQueue::Err, char> {
  template <class ParseContext>
  constexpr ParseContext::iterator parse(ParseContext &ctx) {
    auto it = ctx.begin();
    return it;
  }

  template <class FmtContext>
  FmtContext::iterator format(Telemetry::TelemetryQueue::Err e,
                              FmtContext &ctx) const {
    std::ostringstream oss;
    oss << std::quoted(std::string{e});
    return std::ranges::copy(std::move(oss).str(), ctx.out()).out;
  }
};
