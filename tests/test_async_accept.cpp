#include "sio/io_uring/file_handle.hpp"
#include "sio/io_uring/socket_handle.hpp"
#include "sio/ip/address.hpp"
#include "sio/ip/endpoint.hpp"
#include "sio/ip/tcp.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/sequence_concepts.hpp"

#include "common/test_receiver.hpp"

#include <exec/linux/io_uring_context.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/task.hpp>
#include <exec/when_any.hpp>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>
#include <sys/socket.h>

using namespace sio;

template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  stdexec::sync_wait(exec::when_any(std::forward<Sender>(sender), context.run()));
}

TEST_CASE("async_accept concept", "[async_accept]") {
  exec::io_uring_context ctx;

  ip::endpoint ep{ip::address_v4::any(), 80};
  io_uring::acceptor_handle acceptor{ctx, -1, ep};

  auto sequence = sio::async::accept(acceptor);
  STATIC_REQUIRE(exec::sequence_sender<decltype(sequence)>);

  auto op = exec::subscribe(std::move(sequence), any_receiver{});
  STATIC_REQUIRE(stdexec::operation_state<decltype(op)>);
  stdexec::start(op);

  ctx.run_until_empty();
}

TEST_CASE("async_accept should work", "[async_accept]") {
  using namespace stdexec;
  using namespace exec;
  using namespace sio;
  using namespace sio::io_uring;

  exec::io_uring_context ctx;

  stdexec::sender auto accept = sio::async::use_resources(
    [](io_uring::acceptor_handle acceptor) { return ignore_all(sio::async::accept(acceptor)); },
    io_uring::make_deferred_acceptor(&ctx, ip::tcp::v4(), ip::endpoint{ip::address_v4::any(), 1080}));

  stdexec::sender auto connect = async::use_resources(
    [](sio::io_uring::socket_handle client) {
      return sio::async::connect(client, ip::endpoint{ip::address_v4::loopback(), 1080});
    },
    sio::io_uring::make_deferred_socket(&ctx, ip::tcp::v4()));

  ::sync_wait(ctx, exec::when_any(accept, connect));
}
