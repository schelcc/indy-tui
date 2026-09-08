#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ErpMessage.pb.h"

#include "catch2/matchers/catch_matchers.hpp"
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

  bool in_pit = false;

  ErpTelemetry make_cur() const {
    ErpTelemetry m{};
    m.set_timeofday(time_ms);
    m.set_lapdistance(dist);
    m.set_isinpit(in_pit);
    return m;
  }

  ErpTelemetry make_stepping_time() {
    time_ms += time_step_ms;
    return make_cur();
  }

  ErpTelemetry make_stepping_dist() {
    dist += dist_step_m;
    if (dist > TESTLAP_LENGTH_M)
      dist -= TESTLAP_LENGTH_M;
    return make_cur();
  }

  ErpTelemetry make_stepping_all() {
    time_ms += time_step_ms;
    dist += dist_step_m;
    if (dist > TESTLAP_LENGTH_M)
      dist -= TESTLAP_LENGTH_M;
    return make_cur();
  }
};

using Catch::Matchers::WithinRel;

TEST_CASE("Telemetry generator works", "[test-infra]") {
  TelemGenerator gen{};

  gen.time_ms = 0;
  gen.time_step_ms = 100;
  gen.dist = 0;
  gen.dist_step_m = 50;

  CHECK_FALSE(gen.in_pit);
  CHECK(gen.time_ms == 0);
  CHECK_THAT(gen.dist, WithinRel(0.0));

  auto m = gen.make_cur();
  CHECK_FALSE(m.isinpit());
  CHECK(m.timeofday() == 0);
  CHECK_THAT(m.lapdistance(), WithinRel(0.0));

  m = gen.make_stepping_all();
  CHECK_FALSE(gen.in_pit);
  CHECK(gen.time_ms == 100);
  CHECK_THAT(gen.dist, WithinRel(50.0));

  CHECK_FALSE(m.isinpit());
  CHECK(m.timeofday() == 100);
  CHECK_THAT(m.lapdistance(), WithinRel(50.0));

  gen.dist = TESTLAP_LENGTH_M - 2;
  gen.dist_step_m = 5.5;

  m = gen.make_cur();
  CHECK_FALSE(gen.in_pit);
  CHECK(gen.time_ms == 100);
  CHECK_THAT(gen.dist, WithinRel(TESTLAP_LENGTH_M - 2.0));

  CHECK_FALSE(m.isinpit());
  CHECK(m.timeofday() == 100);
  CHECK_THAT(m.lapdistance(), WithinRel(TESTLAP_LENGTH_M - 2.0));

  // 2 behind lap end, so stepping forward 5.5 meters should put us at 3.5
  m = gen.make_stepping_all();
  CHECK_FALSE(gen.in_pit);
  CHECK(gen.time_ms == 200);
  CHECK_THAT(gen.dist, WithinRel(3.5));

  CHECK_FALSE(m.isinpit());
  CHECK(m.timeofday() == 200);
  CHECK_THAT(m.lapdistance(), WithinRel(3.5));
}

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

  SECTION("proper checkpoint progression") {
    // Start at something not equal to 0 mod checkpoint_dist so that if we
    // increment by checkpoint dist we don't wind up on one directly
    gen.dist = 2;

    // Step distance by half of checkpoint distance to verify non-progression
    // when appropriate
    gen.dist_step_m = DriverTelemetry::CHECKPOINT_DIST / 2.0;

    // Start with no prior checkpoint
    REQUIRE_THAT(d.get_last_checkpt(), OptEmpty());

    // Advance, but not beyond checkpoint 1, so we should not reflect as
    // last_checkpoint being 0
    d.take_new_telemetry(gen.make_stepping_all());
    d.refresh();
    REQUIRE_THAT(d.get_last_checkpt(), OptEmpty());

    INFO("Total checkpoints are " << TESTLAP_CHECKPTS);
    INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                  << d.get_last_checkpt().value_or(999));

    // Advance, now beyond checkpoint 1, so last checkpoint should be 0
    d.take_new_telemetry(gen.make_stepping_all());
    d.refresh();
    REQUIRE_THAT(d.get_last_checkpt(), !OptEmpty() && OptHas(0));

    INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                  << d.get_last_checkpt().value_or(999));

    // Start loop at the start of checkpoint 1
    for (size_t i = 1; i < TESTLAP_CHECKPTS - 1; i++) {
      INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                    << d.get_last_checkpt().value_or(999));
      // Advance, putting us halfway to checkpoint i + 1
      d.take_new_telemetry(gen.make_stepping_all());
      d.refresh();

      // Between checkpoint i and i + 1, so last checkpoint would be i - 1
      REQUIRE_THAT(d.get_last_checkpt(), !OptEmpty() && OptHas(i - 1));

      INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                    << d.get_last_checkpt().value_or(999));

      // Advance, putting us just after checkpoint i + 1
      d.take_new_telemetry(gen.make_stepping_all());
      d.refresh();

      // Now last checkpoint should be i
      REQUIRE_THAT(d.get_last_checkpt(), !OptEmpty() && OptHas(i));
    }

    // Should leave us at the final checkpoint, TESTLAP_CHECKPTS - 1

    INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                  << d.get_last_checkpt().value_or(999));

    // Advance, putting us just beyond checkpoint 0, making our last checkpoint
    // the final one
    d.take_new_telemetry(gen.make_stepping_all());
    d.refresh();

    REQUIRE_THAT(d.get_last_checkpt(),
                 !OptEmpty() && OptHas(TESTLAP_CHECKPTS - 1));

    INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                  << d.get_last_checkpt().value_or(999));

    // Advance, keeping us before checkpoint 1
    d.take_new_telemetry(gen.make_stepping_all());
    d.refresh();

    REQUIRE_THAT(d.get_last_checkpt(),
                 !OptEmpty() && OptHas(TESTLAP_CHECKPTS - 1));

    INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                  << d.get_last_checkpt().value_or(999));

    // Advance, putting us past checkpoint 1, wrapping our prev checkpoint back
    // to 0
    d.take_new_telemetry(gen.make_stepping_all());
    d.refresh();

    REQUIRE_THAT(d.get_last_checkpt(), !OptEmpty() && OptHas(0));

    INFO("Current checkpoint is " << d._cur_checkpt << ", last checkpoint is "
                                  << d.get_last_checkpt().value_or(999));
  }

  SECTION("proper checkpoint population") {
    gen.dist_step_m = DriverTelemetry::CHECKPOINT_DIST / 2.0;
    gen.time_step_ms = 200;

    d.take_new_telemetry(gen.make_cur());
    d.refresh();

    // Ensure checkpoints are all empty
    for (size_t i = 0; i < TESTLAP_CHECKPTS; i++) {
      auto c = d.get_checkpoint(i);
      REQUIRE(c.has_value());

      auto const &checkpt = c.value();
      CHECK_THAT(checkpt.time, OptEmpty());
      CHECK(checkpt.age == 0);
    }

    // Advance over each checkpoint
    for (size_t i = 0; i < TESTLAP_CHECKPTS; i++) {
      // Halfway between i and i + 1
      d.take_new_telemetry(gen.make_stepping_all());
      d.refresh();

      // Don't do on the final iteration so that the last time in the generator
      // is the last checkpoint's time
      if (i < TESTLAP_CHECKPTS - 1) {
        // Just past i + 1
        d.take_new_telemetry(gen.make_stepping_all());
        d.refresh();
      }
    }

    // Doesn't check initial checkpoint, as there needs to be special
    // consideration for a short final checkpoint
    for (size_t i = 1; i < TESTLAP_CHECKPTS; i++) {
      auto c = d.get_checkpoint(i % TESTLAP_CHECKPTS);
      REQUIRE(c.has_value());

      auto const &checkpt = c.value();

      INFO("Checkpoint #" << i % TESTLAP_CHECKPTS << " completed at time "
                          << checkpt.time.value_or(1) << " w/ age "
                          << checkpt.age);

      CHECK_THAT(checkpt.time, !OptEmpty() && OptHas(2 * gen.time_step_ms * i));
      CHECK(checkpt.age == 0);
    }

    // Due to the chance for a short final checkpoint we just check that the
    // final time value in the generator match checkpoint 0, the last checkpoint
    // to be crossed
    auto c = d.get_checkpoint(0);

    REQUIRE(c.has_value());
    auto const &checkpt = c.value();

    CHECK_THAT(checkpt.time, !OptEmpty() && OptHas(gen.time_ms));
    CHECK(checkpt.age == 0);
  }
}

TEST_CASE("Corrections for faulty received telemetry", "[telemetry][unit]") {
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

  SECTION("reject fluctuating pit status") {
    gen.in_pit = false;
    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();

    REQUIRE_FALSE(d._telemetry.isinpit());
    REQUIRE_FALSE(d.in_pit);

    gen.in_pit = true;

    // Telem should show is in pit, but driver should show as not
    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();
    REQUIRE(d._telemetry.isinpit());
    REQUIRE_FALSE(d.in_pit);
  }

  SECTION("accept stable pit status only after configured count") {
    gen.in_pit = false;
    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();

    REQUIRE_FALSE(d._telemetry.isinpit());
    REQUIRE_FALSE(d.in_pit);

    gen.in_pit = true;

    // Driver should show as not in pit until at or after min count
    for (size_t i = 0; i < DriverTelemetry::MIN_IN_PIT_CNT - 1; i++) {
      d.take_new_telemetry(gen.make_stepping_time());
      d.refresh();

      REQUIRE(d._telemetry.isinpit());
      REQUIRE_FALSE(d.in_pit);
    }

    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();

    // Should now be at min count
    REQUIRE(d._telemetry.isinpit());
    REQUIRE(d.in_pit);

    // Should remain in pit beyond min count
    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();

    REQUIRE(d._telemetry.isinpit());
    REQUIRE(d.in_pit);
  }

  SECTION("single non-in-pit frame resets pit status") {
    // Build up to and past in-pit, then reset and demonstrate it taking until
    // reaching min count again to reflect as in pit

    gen.in_pit = false;
    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();

    REQUIRE_FALSE(d._telemetry.isinpit());
    REQUIRE_FALSE(d.in_pit);

    gen.in_pit = true;

    // Driver should show as not in pit until at or after min count
    for (size_t i = 0; i < DriverTelemetry::MIN_IN_PIT_CNT + 2; i++) {
      d.take_new_telemetry(gen.make_stepping_time());
      d.refresh();
    }

    REQUIRE(d.in_pit);

    gen.in_pit = false;
    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();
    REQUIRE_FALSE(d.in_pit);

    gen.in_pit = true;
    for (size_t i = 0; i < DriverTelemetry::MIN_IN_PIT_CNT - 1; i++) {
      d.take_new_telemetry(gen.make_stepping_time());
      d.refresh();
      REQUIRE_FALSE(d.in_pit);
    }

    d.take_new_telemetry(gen.make_stepping_time());
    d.refresh();
    REQUIRE(d.in_pit);
  }
}
