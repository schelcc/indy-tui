#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>
#include <print>
#include <pthread.h>
#include <thread>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <ncpp/NCKey.hh>
#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Root.hh>
#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>

#include "core/app_context.hpp"
#include "core/cli.hpp"
#include "core/session.hpp"
#include "core/workers.hpp"

#include "tools/logger.hpp"

int main([[maybe_unused]] const int argc, [[maybe_unused]] const char *argv[]) {
  Tools::Log::SetOut("indycpp.log");
  Tools::Log::SetLevel(Tools::Log::NONE);

  using namespace CLI;

  Parser parser =
      Parser()
          .set_tool_name("indy-tui")
          .set_desc("A cool little toy for following races from the terminal.");

  parser.add_element<Flag>("help")
      .set_help_msg("Show this message.")
      .set_long_opt("help")
      .set_short_opt("h")
      .set_default_state(false);

  parser.add_element<Arg>("mode")
      .set_help_msg("What mode of operation to use. Options are 'live', "
                    "'live-debug', and "
                    "'replay'.")
      .set_handler([](std::string_view const s) -> Core::SessionSource {
        if (s == "live")
          return Core::SessionSource::SERVED_REMOTE;
        else if (s == "live-debug")
          return Core::SessionSource::SERVED_DEBUG;
        else if (s == "replay")
          return Core::SessionSource::LOCAL_REPLAY;
        else
          throw CLI::InputParsingErr(
              std::format("Invalid input for mode: '{}'", s));
      });

  parser.add_element<Option>("url")
      .set_help_msg("What websocket URL to reach for the 'live-debug' mode.")
      .set_long_opt("url")
      .set_short_opt("u");

  parser.add_element<Option>("log output")
      .set_help_msg("Where the log will be written to, if logging is enabled")
      .set_long_opt("log-out")
      .set_default_input("indy-tui.log")
      .set_handler([](std::string_view const s) { return s; });

  parser.add_element<Option>("log level")
      .set_help_msg(
          "What level of information to include in the logging, if "
          "enabled. Options are 'none', 'warn' 'info', 'debug', 'debug2'.")
      .set_long_opt("log-level")
      .set_default_input("info")
      .set_handler([](std::string_view const s) -> Tools::Log::Level {
        if (s == "none")
          return Tools::Log::NONE;
        else if (s == "warn")
          return Tools::Log::WARN;
        else if (s == "info")
          return Tools::Log::INFO;
        else if (s == "debug")
          return Tools::Log::DEBUG;
        else if (s == "debug2")
          return Tools::Log::DEBUG2;
        else
          throw CLI::InputParsingErr(
              std::format("Invalid input for log level: '{}'", s));
      });

  parser.finalize();

  try {
    parser.parse_input(argc, argv);
  } catch (CLI::InputParsingErr &e) {
    std::println("Error encountered parsing command line input: {}", e.what());
    std::println("{}", parser.get_help());
    return EXIT_FAILURE;
  }

  // If help is set, print the help msg and quit. We leave this until now,
  // rather than embedding it into parsing directly and quitting as soon as we
  // have it so that we can allow the rest of parsing to take place, finding any
  // alerting user if there are other errors
  if (parser.get<Flag, bool>("help")) {
    std::println("{}", parser.get_help());
    return EXIT_SUCCESS;
  }

  Tools::Log::SetOut(parser.get<Option, std::string_view>("log output"));
  Tools::Log::SetLevel(parser.get<Option, Tools::Log::Level>("log level"));

  std::vector<ncpp::NCKey> key_queue{};

  std::atomic_flag running{true};
  Core::Session sess(parser.get<Arg, Core::SessionSource>("mode"));

  size_t delay = 2;

  sess.set_delay_sec(delay);

  if (!sess.start_session())
    return EXIT_FAILURE;

  setlocale(LC_ALL, "");
  notcurses_options nc_opts{};

  nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_NO_QUIT_SIGHANDLERS;

  ncpp::NotCurses nc{nc_opts};

  std::jthread output_thread{Workers::InterfaceWorker{nc, running, sess}};
  std::jthread input_thread{
      Workers::KeyWorker{nc, key_queue, running, sess, delay}};

  pthread_setname_np(output_thread.native_handle(), "Display");
  pthread_setname_np(input_thread.native_handle(), "Input");

  App::AppContext::AwaitShutdown();
  Tools::Log::Info(
      std::format("Shutdown requested for reason '{}'",
                  App::AppContext::GetReason().value_or(
                      "no reason provided -- ungraceful shutdown")),
      "MAIN");

  // running.clear();
  input_thread.request_stop();
  output_thread.request_stop();

  input_thread.join();
  output_thread.join();

  sess.end_session();

  Tools::Log::Info("Exiting (graceful)...");
  std::println("Done.");
}
