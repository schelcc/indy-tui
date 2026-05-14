#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/version.hpp>
#include <boost/system/error_code.hpp>
#include <exception>
#include <string_view>

#include "net/net.hpp"

using boost::beast::buffers_to_string;

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace Net::HTTP {

static constexpr std::string_view HTTP_PORT = "80";

Response::Response(http::response<http::dynamic_body> const &resp)
    : status_code(resp.result_int()), result(resp.reason().to_string()),
      body(buffers_to_string(resp.body().data())) {};

Response get(std::string_view host, std::string_view endpoint) {
  net::io_context ioc;

  tcp::resolver resolver(ioc);
  beast::tcp_stream stream(ioc);

  auto const results = resolver.resolve(host, HTTP_PORT);

  stream.connect(results);

  http::request<http::string_body> req{http::verb::get, endpoint.data(), 11};

  req.set(http::field::host, host.data());
  req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

  http::write(stream, req);

  beast::flat_buffer buffer;

  http::response<http::dynamic_body> res;

  http::read(stream, buffer, res);

  beast::error_code ec;
  ec = stream.socket().shutdown(tcp::socket::shutdown_both, ec);

  if (ec && ec != beast::errc::not_connected)
    throw beast::system_error{ec};

  return Response(res);
};

}; // namespace Net::HTTP
