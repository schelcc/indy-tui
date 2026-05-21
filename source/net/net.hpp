#pragma once
#include <string>
#include <string_view>

namespace Net {

namespace HTTP {

struct Response {
  int status_code;
  std::string result;
  std::string body;

  Response(int status, std::string_view in_result, std::string_view in_body)
      : status_code(status), result(in_result), body(in_body) {};
};

Response get(std::string_view, std::string_view);

}; // namespace HTTP

namespace WS {};

}; // namespace Net
