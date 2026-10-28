#include "catch2/catch_test_macros.hpp"

#include <memory>
#include <string>

#include "tools/cow.hpp"

using Tools::CopyOnWrite;

TEST_CASE("cow basic functionality", "[tools]") {
  CopyOnWrite<std::string> obj{"foo"};

  REQUIRE(obj.get_const() == "foo");

  CopyOnWrite<std::string> obj2 = obj;

  REQUIRE(obj.get_const() == obj2.get_const());

  // Both should still be sharing
  REQUIRE(obj.is_sharing());
  REQUIRE(obj2.is_sharing());

  obj2.get_mut().append("bar");

  // Obj should be sharing, but obj2 should've been pulled down
  REQUIRE(obj.is_sharing());
  REQUIRE_FALSE(obj2.is_sharing());

  REQUIRE(obj2.get_const() == "foobar");
  REQUIRE(obj.get_const() == "foo");
}
