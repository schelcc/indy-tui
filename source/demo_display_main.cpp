#include "session.hpp"
#include "tools/logger.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>
#include <print>
#include <string>
#include <string_view>
#include <thread>

static constexpr bool BLOCKING = true;
static constexpr bool NON_BLOCKING = true;

int main() {
  Tools::Log::SetLevel(Tools::Log::DEBUG);

  Core::Session sess(Core::SessionSource::SERVED_DEBUG);

  sess.set_delay_sec(2);

  auto res = sess.start_session();
  if (!res.has_value())
    std::terminate();

  std::atomic_bool running{true};
  std::jthread output_thread{[&sess, &running] {
    while (running) {
      auto next_frame = sess.next_frame();

      if (next_frame.has_value()) {
        std::println("Good dequeue");
      } else {
        switch (next_frame.error().kind) {
        case Telemetry::TelemetryQueue::Err::TOO_RECENT:
          std::println("TOO RECENT: {} / {} ms", sess.get_accrued_delay_ms(),
                       sess.get_delay_sec().value());
          break;
        case Telemetry::TelemetryQueue::Err::LOCK_FAIL:
          std::println("LOCK FAIL");
          break;
        case Telemetry::TelemetryQueue::Err::DELAY_FULL:
          std::println("DELAY FULL");
          break;
        case Telemetry::TelemetryQueue::Err::FRAME_INVALID:
          std::println("FRAME INVALID");
          break;
        }
      }
    }
  }};

  size_t v = 0;
  std::string line;
  while (std::getline(std::cin, line)) {
    v++;
  }

  running = false;

  sess.end_session();
}
