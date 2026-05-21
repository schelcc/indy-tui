#include <format>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "net/net.hpp"
#include "tools/appsync_resolver.hpp"
#include "tools/base64.hpp"
#include "tools/uuid.hpp"

namespace HTTP = Net::HTTP;

namespace Tools {

std::string AppSyncSession::get_registration_body() const {
  return std::format("{{\"id\": \"{}\", "
                     "\"payload\": {{"
                     "\"data\": \"{}\", "
                     "\"extensions\": {{"
                     "\"authorization\": {{"
                     "\"host\": \"{}\", "
                     "\"x-api-key\": \"{}\""
                     "}}}}}}, \"type\": \"start\"}}",
                     uuid(), GQL_SUBSCRIPTION, base_hostname, key);
}

std::string AppSyncSession::get_connection_url() const {
  return std::format("{}?header={}&payload=e30=", wss_url,
                     Tools::b64_encode(get_api_header()));
}

std::string AppSyncSession::get_api_header() const {
  return std::format("{{\"host\":\"{}\",\"x-api-key\":\"{}\"}}", base_hostname,
                     key);
}

AppSyncSession resolve_appsync() {
  HTTP::Response resp =
      HTTP::get(AppSyncSession::APPSYNC_HOST, AppSyncSession::APPSYNC_ENDPOINT);

  // TODO: Refactor err handling once error strategy is ironed out

  std::stringstream body{resp.body};
  AppSyncSession session;

  std::string line{};
  std::string key{};
  std::string value{};

  while (std::getline(body, line)) {
    if (!line.contains("APPSYNC_"))
      continue;

    std::stringstream line_ss{line};

    // Parses '"APPSYNC_<...>": "..."
    std::getline(line_ss, key, '"');
    std::getline(line_ss, key, '"');
    std::getline(line_ss, value, '"');
    std::getline(line_ss, value, '"');

    if (key == "APPSYNC_APIKEY")
      session.key = std::move(value);
    else if (key == "APPSYNC_URI")
      session.base_uri = std::move(value);
  }

  // Correct the appsync name in the retrieved URI
  session.uri = std::regex_replace(session.base_uri, std::regex("appsync-api"),
                                   "appsync-realtime-api");

  // Cut off to just the hostname (w/ both URIs)
  session.hostname =
      std::regex_replace(session.uri, std::regex("https://"), "");
  session.hostname =
      std::regex_replace(session.hostname, std::regex("/graphql"), "");

  session.base_hostname =
      std::regex_replace(session.base_uri, std::regex("https://"), "");
  session.base_hostname =
      std::regex_replace(session.base_hostname, std::regex("/graphql"), "");

  // Swap https for wss for the wss url
  session.wss_url = std::regex_replace(session.uri, std::regex("https"), "wss");

  return session;
}

}; // namespace Tools
