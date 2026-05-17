#include "core/telemetry.hpp"
#include "appsync_resolver.hpp"
#include "tools/logger.hpp"
#include <thread>

namespace Telemetry {

std::string_view ParsePayload(std::string_view payload) {
  return payload.substr(payload.rfind(R"("data":")") + 8,
                        payload.find_last_of('"'));
}

}; // namespace Telemetry
