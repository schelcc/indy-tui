#pragma once

#include "core/formatters.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_templated.hpp>
#include <expected>
#include <format>
#include <sstream>

namespace Tools::Matchers {

template <typename S> struct ExpHasVal : Catch::Matchers::MatcherGenericBase {
  ExpHasVal(S s) : _cmp(s) {}

  template <typename E> bool match(std::expected<S, E> const &other) const {
    return other.value() == _cmp;
  }

  std::string describe() const override {
    return std::format("expected has value '{}'", _cmp);
  }

private:
  S _cmp;
};

template <typename E> struct ExpHasErr : Catch::Matchers::MatcherGenericBase {
  ExpHasErr(E e) : _cmp(e) {}

  template <typename S> bool match(std::expected<S, E> const &other) const {
    return other.error() == _cmp;
  }

  std::string describe() const override {
    return std::format("expected has value '{}'", _cmp);
  }

private:
  E _cmp;
};

struct ExpIsNotErr : Catch::Matchers::MatcherGenericBase {
  template <typename S, typename E>
  bool match(std::expected<S, E> const &other) const {
    return other.has_value();
  }

  std::string describe() const override {
    return "expected does not have error";
  }
};

struct ExpIsErr : Catch::Matchers::MatcherGenericBase {
  template <typename S, typename E>
  bool match(std::expected<S, E> const &other) const {
    return !other.has_value();
  }

  std::string describe() const override { return "expected has error"; }
};

struct OptEmpty : Catch::Matchers::MatcherGenericBase {
  template <typename T> bool match(std::optional<T> const &other) const {
    return !other.has_value();
  }

  std::string describe() const override { return "option is empty"; }
};

template <typename T>
  requires requires(T t) { std::format("{}", t); }
struct OptHas : Catch::Matchers::MatcherGenericBase {
  OptHas(T t) : _t(t) {};

  bool match(std::optional<T> const &other) const {
    return other.value() == _t;
  }

  std::string describe() const override {
    return std::format("option has value \"{}\"", _t);
  }

private:
  T _t;
};

}; // namespace Tools::Matchers
