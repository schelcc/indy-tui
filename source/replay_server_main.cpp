#include "ErpMessage.pb.h"
#include "base64.hpp"
#include "ixwebsocket/IXConnectionState.h"
#include "ixwebsocket/IXWebSocketMessage.h"
#include "ixwebsocket/IXWebSocketMessageType.h"
#include "tools/logger.hpp"
#include <chrono>
#include <fstream>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXUserAgent.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <memory>
#include <print>
#include <thread>

int main() {
  // https://machinezone.github.io/IXWebSocket/usage/#websocket-server-api
  Tools::Log::SetOut("indyreplay.log");

  int port = 8080;
  std::string_view host{"127.0.0.1"};

  std::chrono::duration msg_delay{std::chrono::milliseconds(100)};

  ix::WebSocketServer server(port, host.data());

  std::string_view session_file = "sample_session.bin";

  std::fstream sample{session_file.data()};

  proto::telemetry::ErpMessage telem_msg{};

  Tools::Log::SetLevel(Tools::Log::DEBUG);

  server.setOnClientMessageCallback(
      [&](std::shared_ptr<ix::ConnectionState> connectionState,
          ix::WebSocket &webSocket, const ix::WebSocketMessagePtr &msg) {
        std::println("Remote IP: {}", connectionState->getRemoteIp());

        if (msg->type == ix::WebSocketMessageType::Open) {
          std::println("Connection opened.");

          std::string line;

          // Run forever, looping the session
          while (true) {
            while (std::getline(sample, line)) {
              webSocket.send(std::format("\"data\":\"{}\"}}}}}}}}", line));

              telem_msg.ParseFromString(Tools::b64_decode(line));

              std::println("Sent w/ TELEM#[{}] OVRRES#[{}] CMPLAP#[{}]",
                           telem_msg.telemetrymessages_size(),
                           telem_msg.overallresults_size(),
                           telem_msg.completedlapresult_size());

              if (telem_msg.heartbeats_size() > 0) {
                auto &hbts_0th = telem_msg.heartbeats().Get(0);
                Tools::Log::Debug(
                    std::format("Time of day: {}", hbts_0th.timeofday()));
              }

              std::this_thread::sleep_for(msg_delay);
            }

            // Close and reopen the file at the start of the session
            sample.close();
            sample = std::fstream(session_file.data());
          }

        } else {
          // Shouldn't receive anything
          server.stop();
        }
      });

  auto res = server.listen();
  Tools::Log::Debug("Listening");
  std::println("Listening");
  if (!res.first)
    return -1;

  server.disablePerMessageDeflate();

  server.start();

  server.wait();
}
