#include "sio/const_buffer_span.hpp"

#include <catch2/catch.hpp>

TEST_CASE("const_buffer_subspan - Constructors") {
  std::uint64_t n0{0};
  std::uint64_t n1{1};
  std::array<sio::const_buffer, 2> buffers{
    sio::const_buffer{&n0, sizeof(n0)},
    sio::const_buffer{&n1, sizeof(n1)}
  };
  sio::const_buffer_span span{buffers};
  REQUIRE(span.size() == 2);
  SECTION("first half") {
    sio::const_buffer_subspan sub0 = span.prefix(8).suffix(4);
    sio::const_buffer_subspan sub1 = span.suffix(12).prefix(4);
    CHECK(sub0.size() == 1);
    CHECK(sub1.size() == 1);
    CHECK(sub0 == sub1);
  }
  SECTION("second half") {
    sio::const_buffer_subspan sub0 = span.prefix(12).suffix(4);
    sio::const_buffer_subspan sub1 = span.suffix(8).prefix(4);
    CHECK(sub0.size() == 1);
    CHECK(sub1.size() == 1);
    CHECK(sub0 == sub1);
  }
  SECTION("subspan from default constructed") {
    sio::const_buffer_span span{};
    sio::const_buffer_subspan sub0 = span.prefix(1).suffix(1);
    CHECK(sub0.empty());
  }
  SECTION("subspan makes empty") {
    sio::const_buffer_subspan sub0 = span.prefix(0).suffix(1);
    CHECK(sub0.empty());
  }
}