#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <pthread.h>
#include <queue>
#include <spdlog/common.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
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

#include "core/input.hpp"

int main([[maybe_unused]] const int argc, [[maybe_unused]] const char *argv[]) {
  App::AppContext::Log().set_level(spdlog::level::info);

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

  parser.add_element<Flag>("display demo")
      .set_help_msg("Enter the display-demo mode of operation. For testing "
                    "only, ignores all other args/options/flags.")
      .set_long_opt("disp-demo")
      .set_default_state(false);

  parser.add_element<Arg>("mode")
      .set_help_msg(
          "What mode of operation to use. Options are 'demo', 'live', "
          "'live-debug', and "
          "'local-replay'.")
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

  parser.add_element<Option>("source")
      .set_help_msg("Path to the telemetry session to replay")
      .set_long_opt("source")
      .set_short_opt("s")
      .set_handler([](std::string_view const s) -> std::string {
        std::filesystem::path path(s);

        assert(s.length() > 0);

        if (path.is_relative()) {
          path = std::filesystem::current_path() / path;
        }

        if (!std::filesystem::exists(path))
          throw CLI::InputParsingErr(
              std::format("Session source '{}' does not exist", path.string()));

        if (!std::filesystem::is_regular_file(path))
          throw CLI::InputParsingErr(
              std::format("Session source '{}' is not a file", path.string()));

        if (!std::filesystem::is_regular_file(path))
          throw CLI::InputParsingErr(
              std::format("Session source '{}' is not a file", path.string()));

        {
          std::ifstream f(path.string(), std::ios::in);
          if (!f.good())
            throw CLI::InputParsingErr(std::format(
                "Session source '{}' is not readable", path.string()));
        }

        return path.c_str();
      });

  parser.add_element<Flag>("serve replay")
      .set_help_msg("Start a replay server. Blocks regular usage of the "
                    "application. Requires -s/--source to be set.")
      .set_long_opt("serve-replay")
      .set_default_state(false);

  parser.add_element<Option>("hostname")
      .set_help_msg(
          "Set the hostname to use for connecting to a remote served replay, "
          "or for hosting a replay server. Defaults to '127.0.0.1'")
      .set_long_opt("hostname")
      .set_short_opt("H")
      .set_handler(
          [](std::string_view const s) -> std::string { return s.data(); })
      .set_default_input("127.0.0.1");

  parser.add_element<Option>("port")
      .set_help_msg(
          "Set the port to use for connecting to a remote served replay, for "
          "for hosting a replay server. Defaults to '8080'")
      .set_long_opt("port")
      .set_short_opt("P")
      .set_handler([](std::string_view const s) -> size_t {
        try {
          return std::stoul(s.data());
        } catch (...) {
          throw CLI::InputParsingErr(
              std::format("Port '{}' is not a valid port", s));
        }
      })
      .set_default_input("8080");

  parser.add_element<Option>("url")
      .set_help_msg("What websocket URL to reach for the 'live-debug' mode.")
      .set_long_opt("url")
      .set_short_opt("u");

  // parser.add_element<Option>("log output")
  //     .set_help_msg("Where the log will be written to, if logging is
  //     enabled") .set_long_opt("log-out") .set_default_input("indy-tui.log")
  //     .set_handler([](std::string_view const s) { return s; });

  parser.add_element<Flag>("ncstats")
      .set_help_msg("Whether to print notcurses stats upon graceful exit. "
                    "Stats will not be "
                    "print upon ungraceful exit regardless of this flag.")
      .set_long_opt("nc-stats")
      .set_default_state(false);

  parser.add_element<Option>("log level")
      .set_help_msg(
          "What level of information to include in the logging, if "
          "enabled. Options are 'none', 'warn' 'info', 'debug', 'trace'.")
      .set_long_opt("log-level")
      .set_default_input("info")
      .set_handler([](std::string_view const s) -> spdlog::level::level_enum {
        if (s == "none")
          return spdlog::level::off;
        else if (s == "warn")
          return spdlog::level::warn;
        else if (s == "info")
          return spdlog::level::info;
        else if (s == "debug")
          return spdlog::level::debug;
        else if (s == "trace")
          return spdlog::level::trace;
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

  App::AppContext::Log().set_level(
      parser.get<Option, spdlog::level::level_enum>("log level"));

  /* Everything after this will interact w/ notcurses */

  /*
  // Maybe temporary, install a terminate handler which kills
  // notcurses, so hopefully we can retain the terminal
  std::set_terminate(&on_terminate);
  */

  if (parser.get<Flag, bool>("display demo")) {
    // Will not return to main from here. Set up a minimum runtime context with
    // display and input
    std::vector<ncpp::NCKey> key_queue{};

    std::atomic_flag running{true};
    setlocale(LC_ALL, "");
    notcurses_options nc_opts{};
    nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_NO_QUIT_SIGHANDLERS;
    ncpp::NotCurses nc{nc_opts};

    std::jthread output_thread{Workers::TestInterfaceWorker{nc, running}};
    std::jthread input_thread{Workers::TestKeyWorker{nc, key_queue, running}};

    pthread_setname_np(output_thread.native_handle(), "Display");
    pthread_setname_np(input_thread.native_handle(), "Input");

    App::AppContext::AwaitShutdown();
    App::AppContext::Log().info(
        "Shutdown requested for reason '{}'",
        App::AppContext::GetReason().value_or(
            "no reason provided -- ungraceful shutdown"));

    input_thread.request_stop();
    output_thread.request_stop();

    input_thread.join();
    output_thread.join();

    App::AppContext::Log().info("Exiting");
    return EXIT_SUCCESS;
  }

  if (parser.get<Flag, bool>("serve replay")) {
    // Will not return to main from here
    std::jthread server{[&parser](std::stop_token stop) {
      Workers::ServedReplayWorker worker(
          parser.get<Option, std::string>("source"),
          parser.get<Option, std::string>("hostname"),
          parser.get<Option, size_t>("port"));
      worker(stop);
    }};

    App::AppContext::AwaitShutdown();

    std::println("Shutdown requested for reason '{}'",
                 App::AppContext::GetReason().value_or(
                     "no reason provided -- ungraceful shutdown"));

    server.request_stop();
    return EXIT_SUCCESS;
  }

  std::queue<Input::KeyWithMod> key_queue{};
  std::mutex key_queue_mtx{};
  std::condition_variable_any key_cond{};

  std::atomic_flag running{true};
  Core::Session sess(parser.get<Arg, Core::SessionSource>("mode"));

  size_t delay = 2;

  sess.set_delay_sec(delay);

  if (!sess.start_session())
    return EXIT_FAILURE;

  setlocale(LC_ALL, "");
  notcurses_options nc_opts{};

  nc_opts.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_NO_QUIT_SIGHANDLERS |
                  NCOPTION_SUPPRESS_BANNERS;

  ncpp::NotCurses nc{nc_opts};

  std::jthread output_thread{Workers::InterfaceWorker{
      nc, running, sess, key_cond, key_queue, key_queue_mtx}};

  pthread_setname_np(output_thread.native_handle(), "Display");

  App::AppContext::AwaitShutdown();
  App::AppContext::Log().info("Shutdown requested for reason '{}'",
                              App::AppContext::GetReason().value_or(
                                  "no reason provided -- ungraceful shutdown"));

  std::println("Shutdown requested for reason '{}'",
               App::AppContext::GetReason().value_or(
                   "no reason provided -- ungraceful shutdown"));

  output_thread.request_stop();

  output_thread.join();

  sess.end_session();

  auto stats = nc.stats_alloc();
  nc.get_stats(stats);

  using Time::Duration::ranging_sec_print;
  using Time::Duration::UIntNanoSec;

  // clang-format off
  auto const render_ns		= UIntNanoSec(stats->render_ns);
  auto const render_max_ns	= UIntNanoSec(stats->render_max_ns);
  auto const render_min_ns	= UIntNanoSec(stats->render_min_ns);
  auto const render_avg_ns	= render_ns / stats->renders;

  auto const raster_ns		= UIntNanoSec(stats->raster_ns);
  auto const raster_max_ns	= UIntNanoSec(stats->raster_max_ns);
  auto const raster_min_ns	= UIntNanoSec(stats->raster_min_ns);
  auto const raster_avg_ns	= raster_ns / stats->writeouts;

  auto const writeout_ns	= UIntNanoSec(stats->writeout_ns);
  auto const writeout_max_ns	= UIntNanoSec(stats->writeout_max_ns);
  auto const writeout_min_ns	= UIntNanoSec(stats->writeout_min_ns);
  auto const writeout_avg_ns	= writeout_ns / stats->writeouts;

  double const total_write_ns		= writeout_ns.count() + raster_ns.count();

  double const raster_shareof_write	= (raster_ns.count() / total_write_ns) * 100;
  double const writeout_shareof_write	= (writeout_ns.count() / total_write_ns) * 100;

  double const pct_elisions = (static_cast<double>(stats->cellelisions)
			       / (stats->cellelisions + stats->cellemissions)) * 100;
  // clang-format on

  // Collect stat strings in a vec so we can put them in both log and stdout
  // if requested
  std::vector<std::string> s{
      std::format("render time   -- max: {:>9} / min: {:>9} / avg: {:>9}",
                  ranging_sec_print(render_max_ns),
                  ranging_sec_print(render_min_ns),
                  ranging_sec_print(render_avg_ns)),
      std::format("raster time   -- max: {:>9} / min: {:>9} / avg: {:>9}",
                  ranging_sec_print(raster_max_ns),
                  ranging_sec_print(raster_min_ns),
                  ranging_sec_print(raster_avg_ns)),
      std::format("writeout time -- max: {:>9} / min: {:>9} / avg: {:>9}",
                  ranging_sec_print(writeout_max_ns),
                  ranging_sec_print(writeout_min_ns),
                  ranging_sec_print(writeout_avg_ns)),
      std::format("rasters vs. writes -- {} / {}", stats->renders,
                  stats->writeouts),
      std::format("raster's share of total write   -- {:.2f}%",
                  raster_shareof_write),
      std::format("writeout's share of total write -- {:.2f}%",
                  writeout_shareof_write),
      std::format("cells emitted  -- {}", stats->cellemissions),
      std::format("cells ellided  -- {}", stats->cellelisions),
      std::format("% cell elision -- {:.2f}%", pct_elisions)};

  nc.stop();

  for (auto const &line : s) {
    App::AppContext::Log().info("{}", line);
  }

  if (parser.get<Flag, bool>("ncstats")) {
    for (auto const &line : s)
      std::println("{}", line);
  }

  App::AppContext::Log().info("Exiting");
}
