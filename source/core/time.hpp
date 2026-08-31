#pragma once

#include "core.hpp"
#include <cassert>
#include <chrono>
namespace Time {

using Clock = std::chrono::steady_clock;

using TimePoint = Clock::time_point;

namespace Duration {

using UIntHour = std::chrono::duration<size_t, std::ratio<3600>>;
using UIntMin = std::chrono::duration<size_t, std::ratio<60>>;
using UIntSec = std::chrono::duration<size_t, std::ratio<1>>;
using UIntMilliSec = std::chrono::duration<size_t, std::ratio<1, 1000>>;

using IntSec = std::chrono::duration<int, std::ratio<1>>;
using IntMilliSec = std::chrono::duration<int, std::ratio<1, 1000>>;

using DblSec = std::chrono::duration<double, std::ratio<1>>;
using DblMilliSec = std::chrono::duration<double, std::ratio<1, 1000>>;

/** @brief Concept requiring that type `T` is one of the defined duration types.
 */
template <typename T>
concept DurationLike = Core::IsOneOf<T, UIntSec, UIntMilliSec, IntSec,
                                     IntMilliSec, DblSec, DblMilliSec>;

/** @brief Calculate the amount of time between TimePoints `newest` and
 * `oldest`, returning a duration of the given kind. */
template <DurationLike R>
[[nodiscard]] static inline R TimeBetween(TimePoint const newest,
                                          TimePoint const oldest) {
  assert(newest >= oldest);

  return std::chrono::duration_cast<R>(newest - oldest);
}

}; // namespace Duration

}; // namespace Time
