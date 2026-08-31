#include <catch2/catch_test_macros.hpp>

#include "telemetry/telemetry_queue.hpp"
#include "time.hpp"
#include "tools/matchers.hpp"

using namespace Telemetry;
using namespace Tools::Matchers;

TEST_CASE("Basic single-threaded telemetry_queue functionality",
          "[telemetry]") {
  TelemetryQueue queue{};

  static constexpr size_t INIT_DELAY_S = 2;
  static constexpr size_t INIT_REFRESH_HZ = 10;

  REQUIRE_THAT(queue.get_delay_sec(), ExpIsNotErr() && ExpHasVal(INIT_DELAY_S));

  REQUIRE_THAT(queue.get_refresh_hz(),
               ExpIsNotErr() && ExpHasVal(INIT_REFRESH_HZ));

  REQUIRE(queue.get_total_frames() ==
          (INIT_DELAY_S * INIT_REFRESH_HZ) + DelayInfo::DELAY_SLOP);

  REQUIRE(queue.get_accrued_delay_ms() == Time::Duration::DblMilliSec(0));

  SECTION("changing delay resizes queue") {
    size_t new_delay = 10;

    REQUIRE_THAT(queue.set_delay_sec(new_delay), ExpIsNotErr());

    CHECK_THAT(queue.get_delay_sec(), ExpIsNotErr() && ExpHasVal(new_delay));
    CHECK_THAT(queue.get_refresh_hz(),
               ExpIsNotErr() && ExpHasVal(INIT_REFRESH_HZ));

    // Accrued delay should not have changed
    CHECK(queue.get_accrued_delay_ms() == Time::Duration::DblMilliSec(0));

    CHECK(queue.get_total_frames() ==
          (new_delay * INIT_REFRESH_HZ) + DelayInfo::DELAY_SLOP);
  }

  SECTION("changing refresh rate resizes queue") {
    size_t new_refresh = 15;

    REQUIRE_THAT(queue.set_refresh_hz(new_refresh), ExpIsNotErr());

    CHECK_THAT(queue.get_delay_sec(), ExpIsNotErr() && ExpHasVal(INIT_DELAY_S));
    CHECK_THAT(queue.get_refresh_hz(), ExpIsNotErr() && ExpHasVal(new_refresh));

    // Accrued delay should not have changed
    CHECK(queue.get_accrued_delay_ms() == Time::Duration::DblMilliSec(0));

    CHECK(queue.get_total_frames() ==
          (INIT_DELAY_S * new_refresh) + DelayInfo::DELAY_SLOP);
  }

  SECTION("attempting to set the delay too high does not change the queue") {
    using Err = TelemetryQueue::Err;

    REQUIRE_THAT(queue.set_delay_sec(TelemetryQueue::MAX_DELAY_S + 1),
                 ExpIsErr() && ExpHasErr(Err(Err::ILLEGAL_DELAY)));

    CHECK_THAT(queue.get_delay_sec(), ExpIsNotErr() && ExpHasVal(INIT_DELAY_S));
    CHECK_THAT(queue.get_refresh_hz(),
               ExpIsNotErr() && ExpHasVal(INIT_REFRESH_HZ));

    CHECK(queue.get_total_frames() ==
          (INIT_REFRESH_HZ * INIT_DELAY_S) + DelayInfo::DELAY_SLOP);
  }

  SECTION("attempting to set the refresh too low does not change the queue") {
    using Err = TelemetryQueue::Err;

    REQUIRE_THAT(queue.set_refresh_hz(TelemetryQueue::MIN_REFRESH_HZ - 1),
                 ExpIsErr() && ExpHasErr(Err(Err::ILLEGAL_REFRESH)));

    CHECK_THAT(queue.get_delay_sec(), ExpIsNotErr() && ExpHasVal(INIT_DELAY_S));
    CHECK_THAT(queue.get_refresh_hz(),
               ExpIsNotErr() && ExpHasVal(INIT_REFRESH_HZ));

    CHECK(queue.get_total_frames() ==
          (INIT_REFRESH_HZ * INIT_DELAY_S) + DelayInfo::DELAY_SLOP);
  }
}
