#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ErpMessage.pb.h"

#include "draw.hpp"
#include "telemetry/driver_telemetry.hpp"
#include "tools/matchers.hpp"

using namespace Telemetry;
using namespace Tools::Matchers;

using proto::telemetry::ErpTelemetry;

static constexpr size_t TESTLAP_LENGTH_M = 5050;

static constexpr size_t TESTLAP_CHECKPTS =
    Tools::Numeric::ceil(static_cast<double>(TESTLAP_LENGTH_M) /
                         static_cast<double>(DriverTelemetry::CHECKPOINT_DIST));

static constexpr size_t TESTLAP_DIST_RESTART =
    static_cast<size_t>(TESTLAP_LENGTH_M * DriverTelemetry::LAP_DIST_RESET_PCT);

struct TelemGenerator {
  int time_ms = 0;
  float dist = 5.0;

  int time_step_ms = 1;
  float dist_step_m = 1.0;

  double speed = 125.5;

  ErpTelemetry make_cur() const {
    ErpTelemetry m{};
    m.set_timeofday(time_ms);
    m.set_lapdistance(dist);
    return m;
  }

  ErpTelemetry make_stepping_time() {
    time_ms += time_step_ms;
    return make_cur();
  }

  ErpTelemetry make_stepping_dist() {
    dist += dist_step_m;
    return make_cur();
  }

  ErpTelemetry make_stepping_all() {
    time_ms += time_step_ms;
    dist += dist_step_m;
    return make_cur();
  }
};

TEST_CASE("Basic driver_telemetry functionality", "[telemetry]") {
  DriverTelemetry d{};
  d.set_lap_length(static_cast<double>(TESTLAP_LENGTH_M));
  d.set_checkpoints(TESTLAP_CHECKPTS);

  TelemGenerator gen{};

  SECTION("successful checkpoint setup") {
    // All checkpoints that should be present are
    for (size_t i = 0; i < TESTLAP_CHECKPTS; i++) {
      auto c = d.get_checkpoint(i);
      REQUIRE(c.has_value());
      CHECK(c.value().age == 0);
      CHECK_FALSE(c.value().time.has_value());
    }

    // Checkpoints beyond what should be present are empty
    REQUIRE_FALSE(d.get_checkpoint(TESTLAP_CHECKPTS).has_value());
  }

  SECTION("stale telemetry invalidation") {
    // Hasn't gotten any yet, so shouldn't be valid
    CHECK_FALSE(d.is_telem_valid());

    // Reset stale count to 0
    d.take_new_telemetry(gen.make_stepping_all());

    CHECK(d.is_telem_valid());

    // Increment stale count
    INFO("Stale threshold is " << DriverTelemetry::STALE_TELEM_THRESH);
    for (size_t i = 0; i < DriverTelemetry::STALE_TELEM_THRESH; i++) {
      d.frame_passed();
      INFO("Stale count is " << d._frames_since_telem);
      REQUIRE(d.is_telem_valid());
    }

    // Passing another fame should put us past staleness
    d.frame_passed();
    INFO("Stale count is " << d._frames_since_telem);
    CHECK_FALSE(d.is_telem_valid());
  }
}

TEST_CASE("Corrections for faulty received telemetry", "[telemetry]") {
  DriverTelemetry d{};
  d.set_lap_length(static_cast<double>(TESTLAP_LENGTH_M));
  d.set_checkpoints(TESTLAP_CHECKPTS);

  TelemGenerator gen{};

  SECTION("reject mid-lap drops in distance") {
    // Set mid-lap and realistic distance step
    gen.dist = TESTLAP_LENGTH_M / 2.0;
    gen.dist_step_m = 80;

    d.take_new_telemetry(gen.make_cur());

    REQUIRE_THAT(d._telemetry.lapdistance(),
                 Catch::Matchers::WithinRel(gen.dist));

    // Increment and verify it took, twice
    d.take_new_telemetry(gen.make_stepping_all());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 Catch::Matchers::WithinRel(gen.dist));

    d.take_new_telemetry(gen.make_stepping_all());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 Catch::Matchers::WithinRel(gen.dist));

    // Drop distance, should reject, try twice
    gen.dist_step_m = -80;

    d.take_new_telemetry(gen.make_stepping_all());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 !Catch::Matchers::WithinRel(gen.dist));

    d.take_new_telemetry(gen.make_stepping_all());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 !Catch::Matchers::WithinRel(gen.dist));
  }

  SECTION("reject decrement just before end-of-lap") {
    // Set to just before reset-eligible and reject decrement
    gen.dist = TESTLAP_DIST_RESTART - 2;
    d.take_new_telemetry(gen.make_cur());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 Catch::Matchers::WithinRel(gen.dist));

    gen.dist_step_m = -2;
    d.take_new_telemetry(gen.make_stepping_all());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 !Catch::Matchers::WithinRel(gen.dist));
  }

  SECTION("accept decrement just after end-of-lap") {
    // Set to just after reset-eligible and accept decrement
    gen.dist = TESTLAP_DIST_RESTART + 2;
    d.take_new_telemetry(gen.make_cur());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 Catch::Matchers::WithinRel(gen.dist));

    gen.dist = 0.0;
    d.take_new_telemetry(gen.make_stepping_time());
    REQUIRE_THAT(d._telemetry.lapdistance(),
                 Catch::Matchers::WithinRel(gen.dist));
  }
}
