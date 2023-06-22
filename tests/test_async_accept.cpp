#include "sio/io_uring/file_handle.hpp"
#include "sio/io_uring/async_accept.hpp"
#include "sio/net/ip/address.hpp"
#include "sio/net/ip/endpoint.hpp"
#include "sio/net/ip/tcp.hpp"
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
  ctx.run_until_empty();

  ip::endpoint ep{ip::address_v4::any(), 80};
  io_uring::acceptor acceptor{ctx, -1, ep};

  auto sequence = sio::io_uring::async_accept(acceptor);
  STATIC_REQUIRE(exec::sequence_sender<decltype(sequence)>);

  auto op = exec::subscribe(std::move(sequence), any_receiver{});
  STATIC_REQUIRE(stdexec::operation_state<decltype(op)>);
  stdexec::start(op);

  ctx.run_until_empty();
}

template <class Proto>
auto make_deferred_socket(exec::io_uring_context* ctx, Proto proto) {
  return sio::make_deferred<sio::io_uring::socket_resource<Proto>>(ctx, proto);
}

TEST_CASE("async_accept should work", "[async_accept]") {
  using namespace stdexec;
  using namespace exec;
  using namespace sio;
  using namespace sio::io_uring;

  exec::io_uring_context ctx;

  ip::endpoint ep{ip::address_v4::any(), 1080};
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(fd != -1);
  CHECK(::bind(fd, ep.data(), ep.size()) != -1);
  CHECK(::listen(fd, 1024) != -1);

  io_uring::acceptor acceptor{ctx, fd, ep};

  stdexec::sender auto accept = ignore_all(async_accept(acceptor));

  int client = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(client != -1);
  io_uring::socket_handle client_handle{ctx, client};

  io_uring::io_scheduler scheduler{&ctx};

  stdexec::sender auto connect = sio::async::use_resources(
    [](sio::io_uring::socket_handle client) {
      return sio::async::connect(client, ip::endpoint{ip::address_v4::loopback(), 1080});
    },
    make_deferred_socket(&ctx, sio::ip::tcp::v4()));

  ::sync_wait(ctx, exec::when_any(accept, connect));
}
