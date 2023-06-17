#include "sio/io_uring/socket_handle.hpp"
#include "sio/net_concepts.hpp"
#include "sio/deferred.hpp"
#include "sio/net/ip/tcp.hpp"

#include <catch2/catch.hpp>


template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  stdexec::sync_wait(
    stdexec::when_all(std::forward<Sender>(sender), context.run(exec::until::empty)));
}

template <class Proto>
auto make_deferred_socket(exec::io_uring_context* ctx, Proto proto) {
  return sio::make_deferred<sio::io_uring::socket_resource<Proto>>(ctx, proto);
}

TEST_CASE("socket_handle - Open a socket", "[socket_handle]") {
  exec::io_uring_context context{};
  auto socket = make_deferred_socket(&context, sio::ip::tcp::v4());
  sync_wait(context, sio::async::use_resources([](sio::io_uring::socket_handle socket) {
    CHECK(socket.get() > 0);
    return stdexec::just();
  }, std::move(socket)));
}