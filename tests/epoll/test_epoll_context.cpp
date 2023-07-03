#include "sio/epoll/epoll_context.hpp"

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>
#include <sys/socket.h>

using namespace sio;

TEST_CASE("still in progress", "[epoll_context]") {
  sio::epoll_context ctx{};
}
