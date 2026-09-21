#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <ncpp/Plane.hh>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ErpMessage.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "locked.hpp"

#include <ncpp/Plane.hh>

namespace Telemetry {

// Forward decl.
class DriverTelemetry;
class TelemetryFrame;

enum class SessionType { PRACTICE, RACE, QUALIFYING };

enum class FlagStatus {
  WARM = 0,
  YELLOW = 1,
  GREEN = 2,
  RED = 3,
  CHECKERED = 4
};

struct SessionFlag {
  std::string text;
  FlagStatus status;

  static std::string GetText(SessionFlag const &s) { return s.text; }
  static FlagStatus GetStatus(SessionFlag const &s) { return s.status; }
};

struct TrackSession {
  ThreadSafe::Locked<std::optional<std::string>> track_name = std::nullopt;
  ThreadSafe::Locked<std::optional<std::string>> track_type = std::nullopt;
  ThreadSafe::Locked<std::optional<size_t>> num_checkpts = std::nullopt;

  ThreadSafe::Locked<std::optional<std::string>> event_name = std::nullopt;
  ThreadSafe::Locked<std::optional<std::string>> series_name = std::nullopt;
  ThreadSafe::Locked<std::optional<SessionFlag>> flag_status = std::nullopt;
  ThreadSafe::Locked<std::optional<SessionType>> session_type = std::nullopt;
  ThreadSafe::Locked<std::optional<std::string>> session_status = std::nullopt;
  ThreadSafe::Locked<std::optional<std::string>> time_to_go = std::nullopt;
  ThreadSafe::Locked<std::optional<std::string>> track_time = std::nullopt;

  ThreadSafe::Locked<std::optional<int>> completed_laps = std::nullopt;
  ThreadSafe::Locked<std::optional<int>> total_laps = std::nullopt;
  ThreadSafe::Locked<std::optional<double>> lap_length = std::nullopt;

  /** @brief Fill in as many fields as possible given the current information.
   * Doesn't reset one-time fields. */
  void update(proto::telemetry::ErpMessage const &);
};

/** @brief Represents the collection of telemetry composing the leaderboard. */
class TelemetryBoard {
public:
  /** @brief Error type for any TelemetryBoard failures. */
  struct Err {
    enum Kind {
      FRAME_INVALID,
      COLUMN_EXISTS,
      NO_SUCH_COLUMN,
      MISSING_TELEM,
      MISSING_RES,
      MISSING_LAP
    } kind;
  };

  enum class Direction {
    LEFT,
    RIGHT,
  };

private:
  std::shared_mutex _driver_vec_mtx;
  std::shared_mutex _driver_map_mtx;

  std::vector<DriverTelemetry> _drivers;
  std::unordered_map<std::string, size_t> _driver_map{};

  std::unordered_map<std::string_view, size_t> _column_lookup{};
  std::shared_mutex _columns_mtx;

  std::atomic_int col_x{1};
  std::atomic_int col_y{1};
  std::atomic_int num_rows{34};

  /** @brief Re-match the car-number to driver indices in the driver map. */
  void reassociate_drivers() noexcept;

public:
  /// @brief Information about the current track session.
  TrackSession session_info{};

  /** @brief Fill in newly received telemetry information. Returns an expected
   * with the error encountered, if any. */
  std::expected<void, Err>
  inform_new_frame(std::unique_ptr<TelemetryFrame> &&) noexcept;

  /** @brief Re-order the drivers based on the ordering in use and
   * perform full-board calculations, then return a LockPair
   * containing a reference to the drivers vec and an active
   * shared_lock on it. */
  ThreadSafe::LockPair<std::vector<DriverTelemetry> const &> reorder_and_get();
};

}; // namespace Telemetry
