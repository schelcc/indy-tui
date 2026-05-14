#pragma once
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/message.hpp>
#include <string>
#include <string_view>

namespace Net {

namespace HTTP {

struct Response {
  int status_code;
  std::string result;
  std::string body;

  Response(boost::beast::http::response<boost::beast::http::dynamic_body> const
               &resp);
};

Response get(std::string_view, std::string_view);

}; // namespace HTTP

namespace WS {};

}; // namespace Net
