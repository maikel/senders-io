#include "sio/io_uring/file_handle.hpp"
#include "sio/io_uring/async_accept.hpp"
#include "sio/net/ip/address.hpp"
#include "sio/net/ip/endpoint.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/sequence_concepts.hpp"

#include "common/test_receiver.hpp"

#include <exec/linux/io_uring_context.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/task.hpp>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>
#include <sys/socket.h>

using namespace sio;

template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  stdexec::sync_wait(
    stdexec::when_all(std::forward<Sender>(sender), context.run(exec::until::empty)));
}

TEST_CASE("async_accept concept", "[async_accept]") {
  exec::io_uring_context ctx;
  ctx.run_until_empty();

  ip::endpoint ep{ip::address_v4::any(), 80};
  io_uring::acceptor acceptor{ctx, -1, ep};

  auto sequence = async_accept(acceptor);
  STATIC_REQUIRE(exec::sequence_sender<decltype(sequence)>);

  auto op = exec::subscribe(std::move(sequence), any_receiver{});
  STATIC_REQUIRE(stdexec::operation_state<decltype(op)>);
}

TEST_CASE("async_accept should work", "[async_accept]") {
  using namespace stdexec;
  using namespace exec;
  using namespace sio;
  using namespace sio::io_uring;

  exec::io_uring_context ctx;
  ctx.run_until_empty();

  ip::endpoint ep{ip::address_v4::any(), 1080};
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(fd != -1);
  CHECK(::bind(fd, ep.data(), ep.size()) != -1);
  CHECK(::listen(fd, 1024) != -1);

  io_uring::acceptor acceptor{ctx, fd, ep};

  stdexec::sender auto sndr = ignore_all(async_accept(acceptor));
  // ::sync_wait(ctx, std::move(sndr));
}
