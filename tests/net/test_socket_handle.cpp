#include "sio/io_uring/socket_handle.hpp"
#include "sio/net_concepts.hpp"
#include "sio/deferred.hpp"
#include "sio/net/ip/tcp.hpp"

#include <exec/single_thread_context.hpp>

#include <catch2/catch.hpp>

template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  auto stop_on_done = stdexec::then(std::forward<Sender>(sender), [&context](auto&&...) { context.request_stop(); });
  auto work = stdexec::when_all(stop_on_done, context.run(exec::until::stopped));
  stdexec::sync_wait(std::move(work));
}

template <class Proto>
auto make_deferred_socket(exec::io_uring_context* ctx, Proto proto) {
  return sio::make_deferred<sio::io_uring::socket_resource<Proto>>(ctx, proto);
}

TEST_CASE("socket_handle - Open a socket", "[socket_handle]") {
  exec::io_uring_context context{};
  auto socket = make_deferred_socket(&context, sio::ip::tcp::v4());
  sync_wait(
    context,
    sio::async::use_resources(
      [](sio::io_uring::socket_handle socket) {
        CHECK(socket.get() > 0);
        return stdexec::just();
      },
      std::move(socket)));
}

TEST_CASE("socket_handle - Connect to localhost", "[socket_handle][connect]") {
  exec::io_uring_context context{};
  exec::single_thread_context thread{};
  auto server = make_deferred_socket(&context, sio::ip::tcp::v4());
  auto client = make_deferred_socket(&context, sio::ip::tcp::v4());
  sio::ip::endpoint ep{sio::ip::address_v4::loopback(), 4242};
  sync_wait(
    context,
    sio::async::use_resources(
      [&](auto server, auto client) {
        int one = 1;
        REQUIRE(::setsockopt(server.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) == 0);
        REQUIRE(::bind(server.get(), ep.data(), ep.size()) == 0);
        REQUIRE(::listen(server.get(), 100) == 0);
        auto accept = stdexec::transfer_just(thread.get_scheduler(), server)
                    | stdexec::then([](auto server) noexcept {
                        int rc = ::accept(server.get(), nullptr, nullptr);
                        CHECK(rc != -1);
                        if (rc > 0) {
                          ::close(rc);
                        }
                      });
        using namespace std::chrono_literals;
        auto delayed_connect = stdexec::let_value(
          exec::schedule_after(context.get_scheduler(), 100ms), [client, ep] {
            return sio::async::connect(client, ep) //
                 | stdexec::upon_error([](std::error_code ec) {
                     REQUIRE(false);
                   });
          });
        return stdexec::when_all(accept, delayed_connect);
      },
      std::move(server),
      std::move(client)));
}