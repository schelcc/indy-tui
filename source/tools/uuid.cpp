#include <ios>
#include <sstream>

#include "tools/uuid.hpp"

// https://stackoverflow.com/questions/24365331/how-can-i-generate-uuid-in-c-without-using-boost-library
namespace Tools {

std::string uuid() {
  std::stringstream output_ss{};

  int i;

  output_ss << std::hex;

  for (i = 0; i < 8; i++) {
    output_ss << UUID::dist(UUID::gen);
  }
  output_ss << "-";
  for (i = 0; i < 4; i++) {
    output_ss << UUID::dist(UUID::gen);
  }
  output_ss << "-4";
  for (i = 0; i < 3; i++) {
    output_ss << UUID::dist(UUID::gen);
  }
  output_ss << "-";
  output_ss << UUID::dist2(UUID::gen);
  for (i = 0; i < 3; i++) {
    output_ss << UUID::dist(UUID::gen);
  }
  output_ss << "-";
  for (i = 0; i < 12; i++) {
    output_ss << UUID::dist(UUID::gen);
  }
  return output_ss.str();
}

} // namespace Tools
