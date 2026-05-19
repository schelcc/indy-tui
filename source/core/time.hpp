#pragma once

#include "core.hpp"
#include <chrono>
namespace Time {

using Clock = std::chrono::steady_clock;

using TimePoint = Clock::time_point;

namespace Duration {

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

}; // namespace Duration

}; // namespace Time
