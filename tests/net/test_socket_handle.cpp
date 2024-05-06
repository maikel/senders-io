#include "sio/async_resource.hpp"
#include "sio/concepts.hpp"
#include "sio/io_uring/socket_handle.hpp"
// #include "sio/net_concepts.hpp"
#include "sio/deferred.hpp"
#include "sio/ip/tcp.hpp"

#include <exec/single_thread_context.hpp>
#include <exec/when_any.hpp>

#include <catch2/catch.hpp>
#include <stdexec/__detail/__transform_completion_signatures.hpp>

template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  stdexec::sync_wait(
    exec::when_any(std::forward<Sender>(sender), context.run(exec::until::stopped)));
}

// TEST_CASE("socket_handle - Open a socket", "[socket_handle]") {
//   exec::io_uring_context context{};
//   auto socket = sio::io_uring::socket(&context, sio::ip::tcp::v4());
//   using S = decltype(socket);
//
//   sio::async::use(std::move(socket));
//
//   constexpr bool B = sio::async::async_resource_::no_use_member<decltype(socket)>;
//   constexpr bool B2 = sio::async::with_open_and_close<S>;
//   constexpr bool B3 = sio::async::with_open<S, stdexec::empty_env>;
//
//
//   using Token = sio::async::token_of_t<S, stdexec::empty_env>;
//   using X = sio::call_result_t<sio::async::open_t, S&>;
//
//   Token token{context, 1, sio::ip::tcp::v4()};
//   using CloseRet = decltype(sio::async::close(token));
//
//   static_assert(sio::async::sender_of_void<CloseRet>);
//
//   static_assert(stdexec::sender_of< CloseRet, stdexec::set_value_t() >);
//   static_assert(stdexec::__single_value_sender< CloseRet, stdexec::empty_env >);
//
//
//   static_assert(sio::async::with_open_and_close<S>);
//   static_assert(sio::async::with_open<S, stdexec::empty_env>);
//
//   token.close();
//   sio::async::close(token);
//
//
//   using Close = sio::call_result_t<sio::async::close_t, Token&>;
//
//   constexpr bool H0 = sio::async::async_resource_::has_close_member_cpo<Token&>;
//
//
//   sync_wait(
//     context,
//     sio::async::use_resources(
//       [](auto socket) {
//         CHECK(socket.get() > 0);
//         return stdexec::just();
//       },
//       std::move(socket)));
// }

// TEST_CASE("socket_handle - Open a socket", "[socket_handle]") {
//   exec::io_uring_context context{};
//   auto socket = sio::io_uring::socket(&context, sio::ip::tcp::v4());
//   sync_wait(
//     context,
//     sio::async::use_resources(
//       [](auto socket) {
//         CHECK(socket.get() > 0);
//         return stdexec::just();
//       },
//       std::move(socket)));
// }

// TEST_CASE("socket_handle - Connect to localhost", "[socket_handle][connect]") {
//   exec::io_uring_context context{};
//   exec::single_thread_context thread{};
//   auto server = sio::io_uring::socket(&context, sio::ip::tcp::v4());
//   auto client = sio::io_uring::socket(&context, sio::ip::tcp::v4());
//   sio::ip::endpoint ep{sio::ip::address_v4::loopback(), 4242};
//   sync_wait(
//     context,
//     sio::async::use_resources(
//       [&](auto server, auto client) {
//         int one = 1;
//         REQUIRE(::setsockopt(server.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) == 0);
//         REQUIRE(::bind(server.get(), ep.data(), ep.size()) == 0);
//         REQUIRE(::listen(server.get(), 100) == 0);
//         auto accept = stdexec::transfer_just(thread.get_scheduler(), server)
//                     | stdexec::then([](auto server) noexcept {
//                         int rc = ::accept(server.get(), nullptr, nullptr);
//                         CHECK(rc != -1);
//                         if (rc > 0) {
//                           ::close(rc);
//                         }
//                       });
//         using namespace std::chrono_literals;
//         auto delayed_connect = stdexec::let_value(
//           exec::schedule_after(context.get_scheduler(), 100ms), [client, ep] {
//             return sio::async::connect(client, ep) //
//                  | stdexec::upon_error([](std::error_code ec) { REQUIRE(false); });
//           });
//         return stdexec::when_all(accept, delayed_connect);
//       },
//       std::move(server),
//       std::move(client)));
// }
