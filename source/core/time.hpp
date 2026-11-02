#pragma once

#include <cassert>
#include <chrono>

#include "core/core.hpp"

namespace Time {

using Clock = std::chrono::steady_clock;

using TimePoint = Clock::time_point;

namespace Duration {

// clang-format off
using UIntHour		= std::chrono::duration<size_t, std::ratio<3600>>;
using UIntMin		= std::chrono::duration<size_t, std::ratio<60>>;
using UIntSec		= std::chrono::duration<size_t, std::ratio<1>>;
using UIntMilliSec	= std::chrono::duration<size_t, std::ratio<1, 1'000>>;
using UIntMicroSec	= std::chrono::duration<size_t, std::ratio<1, 1'000'000>>;
using UIntNanoSec	= std::chrono::duration<size_t, std::ratio<1, 1'000'000'000>>;

using UIntDurations = std::tuple<UIntNanoSec, UIntMicroSec,
				 UIntMilliSec, UIntSec,
				 UIntMin, UIntHour>;

template <typename T>
concept UIntDurationKind = Core::IsOneOf<T, UIntHour, UIntMin,
					    UIntSec, UIntMilliSec,
					    UIntMicroSec, UIntNanoSec>;

using UIntSecondTypes = std::tuple<UIntNanoSec, UIntMicroSec, UIntMilliSec, UIntSec>;

using IntHour		= std::chrono::duration<int, std::ratio<3600>>;
using IntMin		= std::chrono::duration<int, std::ratio<60>>;
using IntSec		= std::chrono::duration<int, std::ratio<1>>;
using IntMilliSec	= std::chrono::duration<int, std::ratio<1, 1'000>>;
using IntMicroSec	= std::chrono::duration<int, std::ratio<1, 1'000'000>>;
using IntNanoSec	= std::chrono::duration<int, std::ratio<1, 1'000'000'000>>;

using IntDurations = std::tuple<IntNanoSec, IntMicroSec,
				IntMilliSec, IntSec,
				IntMin, IntHour>;

template <typename T>
concept IntDurationKind = Core::IsOneOf<T, IntHour, IntMin,
					   IntSec, IntMilliSec,
                                           IntMicroSec, IntNanoSec>;


using DblHour		= std::chrono::duration<double, std::ratio<3600>>;
using DblMin		= std::chrono::duration<double, std::ratio<60>>;
using DblSec		= std::chrono::duration<double, std::ratio<1>>;
using DblMilliSec	= std::chrono::duration<double, std::ratio<1, 1'000>>;
using DblMicroSec	= std::chrono::duration<double, std::ratio<1, 1'000'000>>;
using DblNanoSec	= std::chrono::duration<double, std::ratio<1, 1'000'000'000>>;

using DblDurations = std::tuple<DblNanoSec, DblMicroSec,
				DblMilliSec, DblSec,
				DblMin, DblHour>;

template <typename T>
concept DblDurationKind = Core::IsOneOf<T, DblHour, DblMin,
					   DblSec, DblMilliSec,
                                           DblMicroSec, DblNanoSec>;

/** @brief Concept requiring that type `T` is one of the defined
 * duration types. */
template <typename T>
concept DurationLike =
    UIntDurationKind<T> || IntDurationKind<T> || DblDurationKind<T>;
// clang-format on

template <
    typename T, std::intmax_t I,
    typename Dur = std::chrono::duration<double, std::ratio<1, I>>,
    typename NextDur = std::conditional_t<
        (I > 1), std::chrono::duration<double, std::ratio<1, I / 1000>>, void>>
  requires((I % 10 == 0) || (I == 1))
std::string
ranging_sec_print(std::chrono::duration<T, std::ratio<1, I>> d_in) noexcept {
  using std::chrono::duration;
  using std::chrono::duration_cast;

  Dur d = std::chrono::duration_cast<Dur>(d_in);

  if constexpr (I == 1) {
    return std::format("{}", d);
  } else {
    static_assert(I >= 1000);
    if (d.count() >= 1000.0) {
      return ranging_sec_print(duration_cast<NextDur>(d));
    } else {
      return std::format("{}", d);
    }
  }
}

// template <typename T>
//   requires(Core::tuple_contains_type<T>(std::declval<UIntSecondTypes>()))
// std::string flexible_print(T &&t) {
//   if constexpr (Core::is_last_element<T>(std::declval<UIntDurations>())) {
//     return std::format("{}", std::forward<T>(t));
//   } else {
//     auto t_next = Core::as_next_element<T,
//     UIntSecondTypes>(std::forward<T>(t)); t_next = (t_next *
//     decltype(t_next)::period::num) /
//              decltype(t_next)::period::denom;
//     if (t_next.count() >= 1000)
//       return flexible_print(std::move(t_next));
//     else
//       return std::format("{}", t_next);
//   }
// }

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
