#include <exception>
#include <string_view>

#include <ixwebsocket/IXHttp.h>
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXWebSocketHttpHeaders.h>

#include "net/net.hpp"

namespace Net::HTTP {

Response get(std::string_view host, std::string_view endpoint) {
  ix::HttpClient httpClient;
  ix::HttpRequestArgsPtr args = httpClient.createRequest();

  ix::HttpResponsePtr out =
      httpClient.get(std::format("http://{}/{}", host, endpoint), args);

  return Response(out->statusCode, out->errorMsg, out->body);
};

}; // namespace Net::HTTP
