// #include "ErpMessage.pb.h"
// #include "base64.hpp"
// #include <string_view>
// #include <unordered_map>

// export module TelemetryFrame;

// namespace Telemetry {

// /** @brief Represents a single instance of received telemetry. Not
// thread-safe,
//  * proper locking must be employed by the user.  */
// export class Frame {
//   bool _valid{false};
//   proto::telemetry::ErpMessage _msg;

//   std::unordered_map<std::string, std::string> _driver_names_cache;

// public:
//   /** @brief Determine if this frame is valid. */
//   [[nodiscard]] bool is_valid() const { return _valid; }

//   /** @brief Swap only the telemetry of this frame with that of another.
//    * Invalidates this frame. Cached */
//   void swap_telem(Frame &other) {
//     std::swap(_msg, other._msg);
//     _valid = false;
//   }

//   /** @brief Populate the frame with the given base64 encoded telemetry. */
//   void populate(std::string_view telem) {
//     _msg.ParseFromString(Tools::b64_decode(telem));
//     _valid = true;
//   }
// };

// }; // namespace Telemetry
