#include <algorithm>
#include <cassert>
#include <memory>
#include <mutex>
#include <ncpp/NCKey.hh>
#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Root.hh>
#include <notcurses/notcurses.h>
#include <shared_mutex>
#include <string>
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

}; // namespace Telemetry
