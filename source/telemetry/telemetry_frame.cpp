#include <algorithm>
#include <cassert>
#include <execution>
#include <memory>
#include <mutex>
#include <ncpp/CellStyle.hh>
#include <ncpp/NCBox.hh>
#include <ncpp/NCKey.hh>
#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Root.hh>
#include <notcurses/ncseqs.h>
#include <notcurses/notcurses.h>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>

#include "telemetry/driver_telemetry.hpp"
#include "telemetry/telemetry_board.hpp"
#include "telemetry/telemetry_frame.hpp"

#include "ErpMessage.pb.h"
#include "time.hpp"

using proto::telemetry::ErpMessage;

using Time::Clock;

namespace Telemetry {

TelemetryFrame::TelemetryFrame(std::string &&payload) noexcept
    : _msg(ErpMessage{}), _valid(false), _mod_time(Time::Clock::now()) {
  _msg.ParseFromString(payload);

  _valid = true;
}

void TelemetryBoard::reassociate_drivers() noexcept {
  // TODO: Figure out error handling
  std::shared_lock map_lock(_driver_map_mtx);
  {
    std::shared_lock vec_lock(_driver_vec_mtx);
    assert(_drivers.size() == _driver_map.size());
  }

  if (_drivers.empty())
    return;

  size_t idx{0};
  std::for_each(std::cbegin(_drivers), std::cend(_drivers),
                [this, &idx](DriverTelemetry const &d) {
                  assert(_driver_map.contains(d.get_car_num().data()));
                  _driver_map.at(d.get_car_num().data()) = idx++;
                });

  // Make sure nothing weird happened
  assert(idx == _drivers.size());
}

std::expected<void, TelemetryBoard::Err> TelemetryBoard::inform_new_frame(
    std::unique_ptr<TelemetryFrame> &&frame) noexcept {
  if (!frame->is_valid())
    return std::unexpected(Err(TelemetryBoard::Err::FRAME_INVALID));

  ErpMessage message{frame->take_from()};

  // Check whether the driver is in the map, adding it if not. If the carnumber
  // couldn't be found, return false. If successful, return true. Can add in a
  // condition and guard initialized with short circuiting
  auto check_and_populate = [this](auto const &iter) {
    assert(iter.has_carnumber());

    std::string const &car_num = iter.carnumber();

    if (!_driver_map.contains(car_num)) {
      _driver_map[car_num] = _drivers.size();
      _drivers.emplace_back(DriverTelemetry{});
    }

    return true;
  };

  std::unique_lock map_lock(_driver_map_mtx);
  std::unique_lock vec_lock(_driver_vec_mtx);

  if (message.telemetrymessages_size() > 0) {
    // Increment the stale count on all drivers, then we'll reset the ones we
    // come accross (only if we got any telemetry)
    std::for_each(std::begin(_drivers), std::end(_drivers),
                  [](DriverTelemetry &d) { d.frame_passed(); });

    std::for_each(message.mutable_telemetrymessages()->begin(),
                  message.mutable_telemetrymessages()->end(),
                  [this, &check_and_populate](auto iter) {
                    assert(check_and_populate(iter));

                    _drivers.at(_driver_map.at(iter.carnumber()))
                        .take_new_telemetry(std::move(iter));
                  });
  }

  // Populate the driver table with the overall results, if included
  std::for_each(message.mutable_overallresults()->begin(),
                message.mutable_overallresults()->end(),
                [this, &check_and_populate](auto iter) {
                  assert(check_and_populate(iter));

                  _drivers.at(_driver_map.at(iter.carnumber()))
                      .take_new_results(std::move(iter));
                });

  // Populate the driver table with the completed lap information, if included
  std::for_each(message.mutable_completedlapresult()->begin(),
                message.mutable_completedlapresult()->end(),
                [this, &check_and_populate](auto iter) {
                  assert(check_and_populate(iter));

                  _drivers.at(_driver_map.at(iter.carnumber()))
                      .take_new_lap(std::move(iter));
                });

  return {};
}

void TelemetryBoard::draw_basic(std::shared_ptr<ncpp::Plane> plane,
                                int &start_row) {

  // Order drivers by rank
  {
    std::unique_lock lock(_driver_vec_mtx);
    std::sort(std::begin(_drivers), std::end(_drivers),
              DriverTelemetry::OrderByRank);
  }

  // NOTE: This *should* be fine as they are both readers only of the driver vec
  // Will be joined at destruction

  reassociate_drivers();

  std::shared_lock lock(_driver_vec_mtx);
  std::for_each(std::cbegin(_drivers), std::cend(_drivers),
                [&plane, &start_row](DriverTelemetry const &d) {
                  plane->putstr(start_row++, 0,
                                std::format("{:>2}. {:^23} {:>08.2f}",
                                            d.get_rank(), d.get_name(),
                                            d.get_speed())
                                    .data());
                });
}

void TelemetryBoard::draw_columns() {

  // Order drivers by rank
  {
    std::unique_lock lock(_driver_vec_mtx);
    std::sort(std::begin(_drivers), std::end(_drivers),
              DriverTelemetry::OrderByRank);
  }

  reassociate_drivers();

  std::shared_lock lock(_column_planes_mtx);
  auto v = std::views::zip(_column_planes, _columns);
  std::for_each(std::execution::par, std::begin(v), std::end(v),
                [&, this](auto plane_col_pair) -> void {
                  ColumnPlane &col_plane = std::get<0>(plane_col_pair);
                  ColumnPair &col_pair = std::get<1>(plane_col_pair);

                  std::scoped_lock col_lock(col_plane.mtx);
                  std::shared_lock driver_lock(_driver_vec_mtx);

                  size_t idx = 0;

                  auto &plane = col_plane.plane;
                  auto &func = col_pair.func;
                  auto &name = col_pair.name;

                  plane->perimeter_rounded(ncpp::NCBox::CornerMask,
                                           plane->get_channels(), 0);

                  plane->putstr(idx++, 1, name.data());

                  // for (unsigned int i = 0; i < plane->get_dim_y(); i++)
                  //   plane->putwch(idx, i, L'─');

                  std::for_each(std::cbegin(_drivers), std::cend(_drivers),
                                [this, func, &idx, &plane](auto const &driver) {
                                  func(idx++, driver, plane);
                                });
                });
}

// std::expected<void, TelemetryBoard::Err>
// TelemetryBoard::move_column(std::string_view const column_name,
//                             TelemetryBoard::Direction const dir,
//                             size_t const steps) {
//   {
//     // Avoid unique-locking full columns if the column isn't present
//     std::shared_lock lock(_columns_mtx);
//     if (!_column_lookup.contains(column_name))
//       return std::unexpected(
//           TelemetryBoard::Err{TelemetryBoard::Err::NO_SUCH_COLUMN});
//   }

//   {
//     std::unique_lock lock(_columns_mtx);

//     size_t const target_idx{_column_lookup.at(column_name)};

//     int const dir_mult{(dir == Direction::LEFT) ? -1 : 1};

//     int const dst_idx_int =
//         static_cast<int>(target_idx) + (dir_mult * static_cast<int>(steps));

//     size_t const dst_idx = static_cast<size_t>(std::max(
//         0, std::min(static_cast<int>(_columns.size() - 1), dst_idx_int)));

//     assert(dst_idx < _columns.size());

//     std::vector<std::pair<std::string_view, ColumnFunc>> new_columns;

//     // Copy the target column
//     std::pair<std::string_view, ColumnFunc> target_column =
//         _columns.at(target_idx);

//     // Move over all elements before the new insertion point
//     std::move(std::begin(_columns), std::begin(_columns) + dst_idx,
//               std::back_inserter(new_columns));

//     // Copy over moved column
//     new_columns.emplace_back(std::move(target_column));

//     // Move over all elements after the insertion point but before the old
//     point std::move(std::begin(_columns) + dst_idx + 1,
//               std::begin(_columns) + target_idx,
//               std::back_inserter(new_columns));

//     // Move over all elements which are after the old point
//     std::move(std::begin(_columns) + target_idx + 1, std::end(_columns),
//               std::back_inserter(new_columns));

//     _columns = std::move(new_columns);

//     // Fix the column LUT
//     size_t idx{0};
//     std::for_each(std::begin(_columns), std::end(_columns),
//                   [this, &idx](auto const &column_func_pair) {
//                     _column_lookup.at(std::get<0>(column_func_pair)) = idx++;
//                   });

//     return {};
//   }
// }

}; // namespace Telemetry
