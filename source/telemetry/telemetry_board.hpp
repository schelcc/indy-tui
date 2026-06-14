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
          base_plane.get(), num_rows.load() + 1, column_width, col_y.load(),
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

  /** @brief Move an existing column the given number of steps in the given
   * direction. Places column at end if steps overruns the total number of
   * columns. */
  // std::expected<void, Err> move_column(std::string_view const, Direction
  // const,
  //                                      size_t const);

  /** @brief Fill in newly received telemetry information. Returns an expected
   * with the error encountered, if any. */
  std::expected<void, Err>
  inform_new_frame(std::unique_ptr<TelemetryFrame> &&) noexcept;

  /** @brief Draw a basic leaderboard noting the drivers' rank, name, number,
   * and current speed. */
  void draw_basic(std::shared_ptr<ncpp::Plane>, int &start_row);

  /** @brief Invoke registered column functions and draw the result. */
  void draw_columns();
};

}; // namespace Telemetry
