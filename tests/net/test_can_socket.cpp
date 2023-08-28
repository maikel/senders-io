#include <sio/can/raw_protocol.hpp>

#include <sio/sequence/let_value_each.hpp>
#include <sio/sequence/ignore_all.hpp>
#include <sio/sequence/zip.hpp>

#include <exec/when_any.hpp>

#include <catch2/catch.hpp>

TEST_CASE("can - Create raw protocol", "[can]") {
  sio::can::raw_protocol protocol{};
  CHECK(protocol.type() == SOCK_RAW);
  CHECK(protocol.protocol() == CAN_RAW);
  CHECK(protocol.family() == PF_CAN);
}

#include <sio/io_uring/socket_handle.hpp>

TEST_CASE("can - Create socket and bind it", "[can]") {
  exec::io_uring_context ioc{};
  using namespace sio::io_uring;
  using namespace sio;
  io_uring::socket<can::raw_protocol> sock{ioc};
  auto use_socket = zip(async::use(sock), stdexec::just(::can_frame{})) //
                  | let_value_each([](socket_handle<can::raw_protocol> sock, ::can_frame& frame) {
                      sock.bind(can::endpoint{5});
                      frame.can_id = ::htons(0x1234);
                      frame.len = 1;
                      frame.data[0] = 0x42;
                      std::span buffer{&frame, 1};
                      return sock.write(buffer);
                    })
                  | ignore_all();
  stdexec::sync_wait(exec::when_any(use_socket, ioc.run()));
}
