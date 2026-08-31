#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace Tools::Strings {

enum class Err { MISSING_LEFT, MISSING_RIGHT, MISSING_SPLIT };

static inline std::expected<std::pair<std::string_view, std::string_view>, Err>
SplitAtFirst(std::string_view const line, char const split) {
  auto pos = line.find(split);
  if (pos == std::string_view::npos)
    return std::unexpected(Err::MISSING_SPLIT);
  else if (pos == 0)
    return std::unexpected(Err::MISSING_LEFT);
  else if (pos == (line.size() - 1))
    return std::unexpected(Err::MISSING_RIGHT);
  else
    return std::pair<std::string_view, std::string_view>(
        line.substr(0, pos), line.substr(pos + 1, line.size()));
}

}; // namespace Tools::Strings
