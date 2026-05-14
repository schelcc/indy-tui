#pragma once
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace Tools {

struct AppSyncSession {
  // clang-format off
  static constexpr std::string_view CONN_INIT = 
    "{\"type\":\"connection_init\"}";

  static constexpr std::string_view APPSYNC_HOST = 
    "cdn-prod.indycarmobile.com";

  static constexpr std::string_view APPSYNC_ENDPOINT = 
    "/data/2.0.0/prod/ApiConfiguration.json";

  // GraphQL subscription payload - static
  static constexpr std::string_view GQL_SUBSCRIPTION = 
    R"V0G0N({\"query\": \"subscription subscribe { subscribe )V0G0N"
    R"V0G0N((name: \\\"telemetry\\\") { data } }\", \"variables\": {}})V0G0N";
  // clang-format on

  std::string uri;
  std::string base_uri;
  std::string key;
  std::string wss_url;
  std::string hostname;
  std::string base_hostname;

  std::string get_api_header() const;
  std::string get_connection_url() const;
  std::string get_registration_body() const;
};

AppSyncSession resolve_appsync();

}; // namespace Tools
