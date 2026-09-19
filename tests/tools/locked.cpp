#include "catch2/catch_test_macros.hpp"
#include <memory>
#include <string>

#include "tools/locked.hpp"

using ThreadSafe::Locked;

TEST_CASE("Lock/LockPair demo", "[threadsafe][tools]") {
  Locked<std::string> s = "foo";

  CHECK(*s.get_mut() == "foo");
  // CHECK(s.get_mut()->size() == 3);

  s.get_mut()->append("bar");
  CHECK(*s.get_const() == "foobar");

  // s.get_const()->append("bar");

  // Locked<std::unique_ptr<std::string>> p =
  // std::make_unique<std::string>("foo");

  // CHECK(**p.get_const() == "foo");

  // SHOULD be a compilation error
  // p.get_const()->append("foo");

  // CHECK(**p.get_const() == "foo");
}
