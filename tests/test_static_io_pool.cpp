#include <sio/io_uring/static_io_pool.hpp>

#include <catch2/catch.hpp>

TEST_CASE("static_io_pool - Construct and destruct", "[static_io_pool]") {
  sio::static_io_pool pool(1);
  CHECK(pool.threads().size() == 1);
}