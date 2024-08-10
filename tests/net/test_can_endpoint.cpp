#include <sio/can/endpoint.hpp>

#include <catch2/catch_all.hpp>

TEST_CASE("can - Create endpoint", "[can]") {
  sio::can::endpoint endpoint{1};
  CHECK(endpoint.data()->can_family == PF_CAN);
  CHECK(endpoint.data()->can_ifindex == 1);
}