/*
 * Copyright (c) 2023 Maikel Nadolski
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sio/io_uring/socket_handle.hpp"
#include "sio/ip/address.hpp"
#include "sio/ip/endpoint.hpp"
#include "sio/ip/tcp.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/let_value_each.hpp"
#include "sio/net_concepts.hpp"

#include "common/test_receiver.hpp"

#include <catch2/catch_all.hpp>
#include <sys/socket.h>

#include <exec/linux/io_uring_context.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/task.hpp>
#include <exec/when_any.hpp>


using namespace sio;

template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  stdexec::sync_wait(exec::when_any(std::forward<Sender>(sender), context.run()));
}

TEST_CASE("async_accept concept", "[async_accept]") {
  exec::io_uring_context ctx;

  ip::endpoint ep{ip::address_v4::any(), 80};
  io_uring::acceptor_handle acceptor{ctx, -1, ip::tcp::v4(), ep};

  auto sequence = sio::async::accept(acceptor);
  STATIC_REQUIRE(exec::sequence_sender<decltype(sequence), stdexec::empty_env>);

  auto op = exec::subscribe(std::move(sequence), any_sequence_receiver{});
  STATIC_REQUIRE(stdexec::operation_state<decltype(op)>);
  stdexec::start(op);

  ctx.run_until_empty();
}

TEST_CASE("async_accept should work", "[async_accept]") {
  exec::io_uring_context ctx;

  io_uring::acceptor acceptor(&ctx, ip::tcp::v4(), ip::endpoint{ip::address_v4::any(), 1080});
  stdexec::sender auto accept = sio::async::use_resources(
    [](auto acceptor) {
      return sio::async::accept(acceptor) //
           | let_value_each([](auto client) {
               return exec::finally(stdexec::just(client), sio::async::close(client));
             }) //
           | sio::ignore_all();
    },
    acceptor);

  sio::io_uring::socket sock(&ctx, ip::tcp::v4());
  stdexec::sender auto connect = async::use_resources(
    [](auto client) {
      return sio::async::connect(client, ip::endpoint{ip::address_v4::loopback(), 1080});
    },
    sock);

  ::sync_wait(ctx, exec::when_any(accept, connect));
}
