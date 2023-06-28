#include "sio/io_uring/socket_handle.hpp"

#include "sio/ip/tcp.hpp"

#include "sio/sequence/let_value_each.hpp"
#include "sio/sequence/ignore_all.hpp"

#include <exec/repeat_effect_until.hpp>
#include <exec/variant_sender.hpp>
#include <exec/when_any.hpp>

#include <iostream>

template <class ThenSender, class ElseSender>
exec::variant_sender<ThenSender, ElseSender>
  if_then_else(bool condition, ThenSender then, ElseSender otherwise) {
  if (condition) {
    return then;
  }
  return otherwise;
}

auto echo_input(sio::io_uring::socket_handle client) {
  return stdexec::let_value(
    stdexec::just(client, std::array<std::byte, 1024>{}),
    [](auto socket, std::span<std::byte> buffer) {
      return sio::async::read_some(socket, buffer) //
           | stdexec::let_value([=](std::size_t nbytes) {
               return if_then_else(
                 nbytes != 0,
                 sio::async::write(socket, buffer.subspan(0, nbytes)),
                 stdexec::just(0));
             })
           | stdexec::then([](std::size_t nbytes) { return nbytes == 0; })
           | exec::repeat_effect_until()
           | stdexec::then([] { std::cout << "Connection closed.\n"; });
    });
}

int main() {
  exec::io_uring_context context{};
  auto acceptor = sio::io_uring::make_deferred_acceptor(
    &context, sio::ip::tcp::v4(), sio::ip::endpoint{sio::ip::address_v4::any(), 1080});

  auto accept_connections = sio::async::use_resources(
    [&](sio::io_uring::acceptor_handle acceptor) {
      return sio::async::accept(acceptor) //
           | sio::let_value_each([](sio::io_uring::socket_handle client) {
               return echo_input(client);
             })
           | sio::ignore_all();
    },
    acceptor);

  stdexec::sync_wait(exec::when_any(accept_connections, context.run()));
}