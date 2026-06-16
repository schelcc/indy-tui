#pragma once

#include <any>
#include <concepts>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core.hpp"
#include "tools/logger.hpp"

namespace CLI {

struct HandlerTypeErr : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct MissingFieldErr : std::runtime_error {
  using std::runtime_error::runtime_error;
  std::string_view field_name;
};

struct ParserFinalizedErr : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct InputParsingErr : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct ParserElement {
  std::optional<std::string_view> internal_name = {};
  std::optional<std::string_view> help_msg = {};

  template <typename Self>
    requires requires(Self s) { s.element_name; }
  MissingFieldErr missing_field(this Self const &self,
                                std::string_view const field_name) {
    auto e = MissingFieldErr(
        std::format("{} missing field '{}'", self.element_name, field_name));
    e.field_name = field_name;
    return e;
  }

  template <typename Self>
  Self &set_internal_name(this Self &self,
                          std::string_view const internal_name) {
    self.internal_name = internal_name;
    return self;
  }

  template <typename Self>
  std::string_view const checked_get_internal_name(this Self const &self) {
    if (self.internal_name.has_value())
      return self.internal_name.value();
    else
      throw self.missing_field("internal_name");
  }

  template <typename Self>
  Self &set_help_msg(this Self &self, std::string_view const help_msg) {
    self.help_msg = help_msg;
    return self;
  }

  template <typename Self>
  std::string_view const checked_get_help_msg(this Self const &self) {
    if (self.help_msg.has_value())
      return self.help_msg.value();
    else
      throw self.missing_field("help_msg");
  }
};

struct HandledElement {
  std::optional<std::function<std::any(std::string_view)>> handler = {};
  std::optional<std::string_view> default_input = {};
  std::optional<std::string_view> parsed_input = {};

  template <typename Self, typename Func>
    requires std::is_invocable_v<Func, const std::string_view>
  Self &set_handler(this Self &self, Func &&func) {
    self.handler = [func =
                        std::move(func)](std::string_view const s) -> std::any {
      return std::any(std::invoke(func, s));
    };
    return self;
  }

  template <typename T> T get_from_handler(this auto const &self) {
    if (!self.handler.has_value())
      throw self.missing_field("handler");

    // Require that we have an internal name before proceeding
    std::string_view const &internal_name = self.checked_get_internal_name();

    std::any handler_res;

    if (!self.parsed_input.has_value() && !self.default_input.has_value())
      throw self.missing_field("parsed_input");
    else if (!self.parsed_input.has_value())
      handler_res =
          std::invoke(self.handler.value(), self.default_input.value());
    else
      handler_res =
          std::invoke(self.handler.value(), self.parsed_input.value());

    // If we get here, we *must* have successfully called the handler
    assert(handler_res.has_value());

    if (handler_res.type() == typeid(T))
      return std::any_cast<T>(handler_res);
    else
      throw HandlerTypeErr(std::format("{} {}'s handler return type and "
                                       "get_from_handler() return type differ",
                                       self.element_name, internal_name));
  };

  template <typename Self>
  Self &set_parsed_input(this Self &self, std::string_view const parsed_input) {
    self.parsed_input = parsed_input;
    return self;
  }

  template <typename Self>
  Self &set_default_input(this Self &self,
                          std::string_view const default_input) {
    self.default_input = default_input;
    return self;
  }
};

struct OptLikeElement {
  std::optional<std::string_view> short_opt = {};
  std::optional<std::string_view> long_opt = {};
  std::vector<std::string_view> conflicts_with = {};

  template <typename Self>
  Self &set_short_opt(this Self &self, std::string_view const short_opt) {
    self.short_opt = short_opt;
    return self;
  }

  template <typename Self>
  std::string_view const checked_get_short_opt(this Self const &self) {
    if (!self.short_opt.has_value())
      throw self.missing_field("short_opt");
    else
      return self.short_opt.value();
  }

  template <typename Self>
  Self &set_long_opt(this Self &self, std::string_view const long_opt) {
    self.long_opt = long_opt;
    return self;
  }

  template <typename Self>
  std::string_view const checked_get_long_opt(this Self const &self) {
    if (!self.long_opt.has_value())
      throw self.missing_field("long_opt");
    else
      return self.long_opt.value();
  }

  template <typename Self>
  Self &add_conflict(this Self &self, std::string_view const conflict_name) {
    self.conflicts_with.push_back(conflict_name);
    return self;
  }
};

struct Arg : ParserElement, HandledElement {
  static constexpr std::string_view element_name = "Arg";
};

struct Option : ParserElement, HandledElement, OptLikeElement {
  static constexpr std::string_view element_name = "Option";
};

struct Flag : ParserElement, OptLikeElement {
  static constexpr std::string_view element_name = "Flag";

  std::optional<bool> default_state = {};

  // Parsed state is the opposite of the default state
  std::optional<bool> parsed_state = {};

  Flag &set_default_state(bool const default_state_) {
    default_state = default_state_;
    parsed_state = default_state_;
    return *this;
  }

  Flag &set_parsed_state() {
    if (!default_state.has_value())
      throw missing_field("default_state");

    // Parsed state is the opposite of the default state
    parsed_state = !default_state.value();

    return *this;
  }

  bool get_default_state() const {
    if (!default_state.has_value())
      throw missing_field("default_state");

    return default_state.value();
  }

  bool get_parsed_state() const {
    if (!parsed_state.has_value())
      throw missing_field("parsed_state");

    return parsed_state.value();
  }
};

namespace ElementVisitors {

struct LongOptVisitor {
  std::string operator()(auto const &o) {
    return std::string{o.checked_get_long_opt()};
  }
};

}; // namespace ElementVisitors

class Parser {
private:
  std::vector<Arg> _args{};
  std::unordered_map<std::string_view, size_t> _arg_lookup{};

  std::vector<std::variant<Option, Flag>> _opts{};

  std::unordered_map<std::string_view, size_t> _named_opt_lookup{};
  std::unordered_map<std::string_view, size_t> _long_opt_lookup{};
  std::unordered_map<std::string_view, size_t> _short_opt_lookup{};

  std::optional<std::string_view> tool_name;
  std::optional<std::string_view> desc;

  bool finalized{false};

  MissingFieldErr missing_field(std::string_view const field_name) const {
    auto e =
        MissingFieldErr(std::format("Parser missing field '{}'", field_name));
    e.field_name = field_name;
    return e;
  }

public:
  template <std::derived_from<ParserElement> E>
  E &add_element(std::string_view const internal_name) {
    if (finalized)
      throw ParserFinalizedErr(
          "cannot add elements to parser after finalization");
    if constexpr (std::is_same_v<E, Arg>) {
      _args.emplace_back(Arg{});
      _arg_lookup[internal_name] = _args.size() - 1;
      return _args.back().set_internal_name(internal_name);
    } else {
      _opts.emplace_back(std::variant<Option, Flag>{std::in_place_type_t<E>()});
      return std::get<E>(_opts.back()).set_internal_name(internal_name);
    }
  }

  void finalize();

  Parser &set_tool_name(std::string_view const tool_name) {
    this->tool_name = tool_name;
    return *this;
  }

  Parser &set_desc(std::string_view const desc) {
    this->desc = desc;
    return *this;
  }

  std::string const get_help() const;

  void parse_input(int const argc, const char *argv[]);

  template <typename Element, typename Ret>
  Ret get(std::string_view const internal_name) {
    if constexpr (std::is_same_v<Element, Arg>) {
      if (_arg_lookup.contains(internal_name))
        return _args.at(_arg_lookup.at(internal_name)).get_from_handler<Ret>();
    } else {
      if (_named_opt_lookup.contains(internal_name)) {
        auto const &opt = _opts.at(_named_opt_lookup.at(internal_name));
        if (!std::holds_alternative<Element>(opt))
          throw InputParsingErr("Element lookup found mismatch between given "
                                "element type and resolved element type");

        // Flag doesn't have a handler, so handle special case
        if constexpr (std::is_same_v<Element, Flag>)
          return std::get<Flag>(opt).parsed_state.value();
        else
          return std::get<Element>(opt).template get_from_handler<Ret>();
      }
    }

    // If here, no element could be found with that name
    throw InputParsingErr(
        std::format("No {} found with name internal name '{}'",
                    Element::element_name, internal_name));
  }
};
}; // namespace CLI
