#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <ncpp/Plane.hh>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "ErpMessage.pb.h"
#include "columns.hpp"
#include "google/protobuf/repeated_ptr_field.h"

#include <ncpp/Plane.hh>

namespace Telemetry {

// Forward decl.
class DriverTelemetry;
class TelemetryFrame;

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
  template <typename Field>
  using ProtoIter = google::protobuf::RepeatedPtrField<Field>;

  using ColumnFunc = std::function<void(size_t const, DriverTelemetry const &,
                                        std::shared_ptr<ncpp::Plane>)>;

  struct ColumnPair {
    const std::string_view name;
    ColumnFunc func;
  };

  struct ColumnPlane {
    std::mutex mtx{};
    std::shared_ptr<ncpp::Plane> plane;

    ColumnPlane(std::shared_ptr<ncpp::Plane> p) : mtx(), plane(p) {}

    // Copy ctors
    ColumnPlane(ColumnPlane const &o) : mtx(), plane(o.plane) {}
    ColumnPlane &operator=(ColumnPlane const &o) {
      plane = o.plane;
      return *this;
    }

    ColumnPlane() = default;
  };

  struct EventInfo {
    // Reader(s) should take shared lock on this
    std::shared_mutex mtx{};

    // Question: Should a field not existing in an update after it has existed
    // prior unset that field? Or should it hang around?

    // Found in TrackInformation:
    std::optional<std::string> track_name = {};
    std::optional<std::string> track_type = {};

    // Found in HeartBeat:
    std::optional<std::string> event_name = {};
    std::optional<std::string> series_name = {};
    std::optional<std::string> flag_status = {};
    std::optional<std::string> session_type = {};
    std::optional<std::string> session_status = {};
    std::optional<std::string> time_to_go = {};
    std::optional<std::string> track_time = {};
    std::optional<int> completed_laps = {};
    std::optional<int> total_laps = {};

    /** @brief Update fields given a parsed protobuf message. For fields only
     * found in per-driver entries, uses the first available driver.
     * Thread-safe. */
    void update(proto::telemetry::ErpMessage const &);
  } _event_info{};

  std::shared_mutex _driver_vec_mtx;
  std::shared_mutex _driver_map_mtx;

  std::vector<DriverTelemetry> _drivers;
  std::unordered_map<std::string, size_t> _driver_map{};

  std::unordered_map<std::string_view, size_t> _column_lookup{};
  std::vector<ColumnPair> _columns{};
  std::shared_mutex _columns_mtx;

  std::atomic_int col_x{0};
  std::atomic_int col_y{0};
  std::atomic_int num_rows{34};
  std::vector<ColumnPlane> _column_planes;
  std::shared_mutex _column_planes_mtx;

  /** @brief Re-match the car-number to driver indices in the driver map. */
  void reassociate_drivers() noexcept;

public:
  /** @brief Add new column to the board. Does nothing if column is already
   * added. */
  template <Columns::ColumnKind Col>
  std::expected<void, Err> add_column(std::shared_ptr<ncpp::Plane> base_plane,
                                      Col &&col) {
    {
      // Avoid unique-locking the full columns if the column is already present
      std::shared_lock lock(_columns_mtx);
      if (_column_lookup.contains(col.COL_NAME))
        return std::unexpected(Err{Err::COLUMN_EXISTS});
    }

    // Make sure column width is no narrower than the column name
    size_t column_width =
        std::max(static_cast<int>(col.COL_NAME.length()), col.COL_WIDTH);

    // Always add padding for lines
    column_width += 2;

    {
      std::unique_lock lock(_column_planes_mtx);

      _column_planes.push_back(ColumnPlane{std::make_shared<ncpp::Plane>(
          base_plane.get(), base_plane->get_dim_y(), column_width, col_y.load(),
          col_x.load())});

      col_x += column_width;
    }

    std::unique_lock lock(_columns_mtx);
    _column_lookup[col.COL_NAME.data()] = _columns.size();
    _columns.push_back(ColumnPair{
        col.COL_NAME,
        [f = std::move(col)](size_t const row, DriverTelemetry const &d,
                             std::shared_ptr<ncpp::Plane> plane) mutable {
          std::invoke(f, row, d, plane);
        }});
    return {};
  }

  // TODO: Column moving (can just move the planes, shouldn't need to modify the
  // vec)
  /** @brief Fill in newly received telemetry information. Returns an expected
   * with the error encountered, if any. */
  std::expected<void, Err>
  inform_new_frame(std::unique_ptr<TelemetryFrame> &&) noexcept;

  /** @brief Invoke registered column functions and draw the result. */
  void draw_columns();

  /** @brief Draw the track event's (non-driver-specific) information. Things
   * like event name lap count, flag status, time of day, maybe time remaining
   * if applicable. */
  void draw_event_info(std::shared_ptr<ncpp::Plane>);
};

}; // namespace Telemetry
