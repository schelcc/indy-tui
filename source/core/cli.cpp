#include "core/cli.hpp"
#include "core.hpp"
#include <string_view>

static constexpr size_t SEP_WIDTH = 5;

namespace CLI {
std::string const Parser::get_help() const {
  std::stringstream output{};

  if (!finalized)
    throw ParserFinalizedErr(
        "parser must be finalized before calling get_help()");

  if (!tool_name.has_value())
    throw missing_field("tool_name");
  if (!desc.has_value())
    throw missing_field("desc");

  output << tool_name.value() << std::endl;
  output << desc.value() << std::endl;

  auto concat_args = [](std::vector<Arg> const &args) {
    std::stringstream o{};
    for (auto const &a : args) {
      o << std::format(" [{}]", a.checked_get_internal_name());
    }
    return o.str();
  };

  // Usage: $ <tool_name> [OPTIONS] <args...>
  output << std::endl
         << std::format("Usage: $ {} [OPTIONS]{}", tool_name.value(),
                        concat_args(_args))
         << std::endl
         << std::endl;

  output << "Args: " << std::endl;

  using ArgRow = std::tuple<std::string, std::string>;

  std::vector<ArgRow> arg_rows{};

  // Build rows of pairs of strings representing arg spec and description,
  // respectively
  std::transform(std::cbegin(_args), std::cend(_args),
                 std::back_inserter(arg_rows), [](Arg const &arg) -> ArgRow {
                   return ArgRow{arg.checked_get_internal_name(),
                                 arg.checked_get_help_msg()};
                 });

  // Find the longest arg name
  const size_t max_arg =
      std::get<0>(*std::max_element(std::cbegin(arg_rows), std::cend(arg_rows),
                                    [](ArgRow const &a, ArgRow const &b) {
                                      return std::get<0>(a) < std::get<0>(b);
                                    }))
          .length();

  // Find the longest arg description
  const size_t max_arg_desc =
      std::get<1>(*std::max_element(std::cbegin(arg_rows), std::cend(arg_rows),
                                    [](ArgRow const &a, ArgRow const &b) {
                                      return std::get<1>(a) < std::get<1>(b);
                                    }))
          .length();

  // For each row in arg_rows, stream the arg name and description to the output
  // stringstream
  std::transform(std::cbegin(arg_rows), std::cend(arg_rows),
                 std::ostream_iterator<std::string>(output, "\n"),
                 [max_arg, max_arg_desc](ArgRow const &row) -> std::string {
                   std::stringstream o{};
                   o << "  " << std::left << std::setw(max_arg)
                     << std::get<0>(row) << std::setw(SEP_WIDTH) << " "
                     << std::setw(max_arg_desc) << std::get<1>(row);
                   return o.str();
                 });

  output << std::endl;

  output << "Options: " << std::endl;
  using OptRow = std::tuple<std::string, std::string, std::string>;

  auto to_opt_row =
      Core::Overload{[this](std::derived_from<OptLikeElement> auto const &o) {
        std::string shortname = std::string{o.short_opt.value_or("")};
        std::string longname = std::string{o.checked_get_long_opt()};
        std::string desc = std::string{o.checked_get_help_msg()};

        if (o.conflicts_with.size() > 0) {
          std::stringstream conflict_str{};
          conflict_str << " Conflicts with: ";
          std::transform(
              std::cbegin(o.conflicts_with), std::cend(o.conflicts_with),
              std::ostream_iterator<std::string>(conflict_str, " "),
              [this](std::string_view conflict) -> std::string {
                // Display the longopt for conflicts
                return "--" +
                       std::visit(ElementVisitors::LongOptVisitor{},
                                  _opts.at(_named_opt_lookup.at(conflict)));
              });
          desc += conflict_str.str();
        }

        return OptRow{shortname, longname, desc};
      }};

  std::vector<OptRow> opt_rows{};

  // Get all options into a vec to tuples
  std::transform(std::cbegin(_opts), std::cend(_opts),
                 std::back_inserter(opt_rows),
                 [to_opt_row](auto const &opt) -> OptRow {
                   return std::visit(to_opt_row, opt);
                 });

  // Retrieve the max length of each tuple obj
  const size_t max_shortname =
      std::get<0>(*std::max_element(std::cbegin(opt_rows), std::cend(opt_rows),
                                    [](OptRow const &a, OptRow const &b) {
                                      return std::get<0>(a).size() <
                                             std::get<0>(b).size();
                                    }))
          .length();
  const size_t max_longname =
      std::get<1>(*std::max_element(std::cbegin(opt_rows), std::cend(opt_rows),
                                    [](OptRow const &a, OptRow const &b) {
                                      return std::get<1>(a).size() <
                                             std::get<1>(b).size();
                                    }))
          .length();

  std::transform(
      std::cbegin(opt_rows), std::cend(opt_rows),
      std::ostream_iterator<std::string>(output, "\n"),
      [max_shortname, max_longname](OptRow const &row) -> std::string {
        std::stringstream o{};
        const std::string_view shortname = std::get<0>(row);
        const std::string_view longname = std::get<1>(row);
        const std::string_view desc = std::get<2>(row);

        o << "  " << (shortname.length() > 0 ? "-" : " ") << shortname;
        if (max_shortname - shortname.length() > 0)
          o << std::string(max_shortname - shortname.length(), ' ');

        o << " " << (longname.length() > 0 ? "--" : "  ") << longname;
        if (max_longname - longname.length() > 0)
          o << std::string(max_longname - longname.length(), ' ');

        o << std::string(SEP_WIDTH, ' ') << desc;

        return o.str();
      });

  return output.str();
}

void Parser::finalize() {
  // Do nothing if already finalized
  if (finalized)
    return;

  size_t idx{0};
  std::for_each(std::cbegin(_opts), std::cend(_opts),
                [this, &idx](std::variant<Option, Flag> const &opt) {
                  std::visit(
                      [this, idx](auto const &o) {
                        _named_opt_lookup[o.checked_get_internal_name()] = idx;
                        if (o.long_opt.has_value())
                          _long_opt_lookup[o.long_opt.value()] = idx;
                        if (o.short_opt.has_value())
                          _short_opt_lookup[o.short_opt.value()] = idx;
                      },
                      opt);
                  idx++;
                });
  finalized = true;
}

void Parser::parse_input(int const argc, const char *argv[]) {
  if (!finalized)
    throw ParserFinalizedErr(
        "parser must be finalized prior to calling parse_input()");

  size_t arg_pos{0};

  for (int arg_idx{1}; arg_idx < argc; ++arg_idx) {
    std::string_view cur_arg{argv[arg_idx]};

    // Grab next one for whenever needed
    std::optional<std::string_view> next_arg =
        (arg_idx == (argc - 1)) ? std::optional<std::string_view>({})
                                : std::string_view{argv[arg_idx + 1]};

    auto opt_visitor = [&arg_idx](std::optional<std::string_view> const val =
                                      "") {
      return Core::Overload{[val, &arg_idx](Option &o) {
                              if (!val.has_value())
                                throw InputParsingErr(
                                    "option expected input but got none");
                              o.set_parsed_input(val.value());
                              arg_idx++;
                            },
                            [](Flag &f) { f.set_parsed_state(); }};
    };

    if (cur_arg.at(0) == '-') {
      // Arg is a longopt
      if (cur_arg.at(1) == '-') {
        auto const &longopt = cur_arg.substr(2);
        if (!_long_opt_lookup.contains(longopt))
          throw InputParsingErr(std::format("Unknown longopt '{}'", cur_arg));
        std::visit(opt_visitor(next_arg),
                   _opts.at(_long_opt_lookup.at(longopt)));
        continue;
      }

      auto const &shortopt = cur_arg.substr(1);

      // Want to handle multiple shortopts appended
      std::for_each(
          std::cbegin(shortopt), std::cend(shortopt),
          [this, next_arg, opt_visitor](char const cur_shortopt) {
            if (!_short_opt_lookup.contains(std::string{cur_shortopt}))
              throw InputParsingErr(
                  std::format("Unknown shortopt '-{}'", cur_shortopt));
            std::visit(opt_visitor(next_arg), _opts.at(_short_opt_lookup.at(
                                                  std::string{cur_shortopt})));
          });

      continue;
    }

    // Arg is a positional arg
    if (arg_pos >= _args.size())
      throw InputParsingErr("Too many arguments");

    _args.at(arg_pos++).set_parsed_input(cur_arg);
  }
}

}; // namespace CLI
