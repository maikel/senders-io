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
#include "sio/async_resource.hpp"
#include "common/test_receiver.hpp"

#include <catch2/catch.hpp>
#include <exec/sequence_senders.hpp>
#include <stdexec/__detail/__transform_completion_signatures.hpp>

struct Token {
  auto close() const noexcept {
    return stdexec::just();
  }
};

struct Resource {
  auto open() const noexcept {
    return stdexec::just(Token{});
  }
};

TEST_CASE("async_resource - sequence", "[async_resource]") {
  STATIC_REQUIRE(sio::async::with_open_and_close<Resource, stdexec::empty_env>);
  Resource res{};
  auto seq = sio::async::use(res);
  using sequence_t = decltype(seq);
  STATIC_REQUIRE(exec::sequence_sender_in<sequence_t, stdexec::empty_env>);
  STATIC_REQUIRE(exec::sequence_sender_to<sequence_t, any_sequence_receiver>);
  auto op = exec::subscribe(std::move(seq), any_sequence_receiver{});
  stdexec::start(op);
}

TEST_CASE("async_resource - use_resources", "[async_resource]") {
  auto sndr = sio::async::use_resources([](Token) { return stdexec::just(42); }, Resource());
  auto result = stdexec::sync_wait(std::move(sndr));
  CHECK(result);
  auto [value] = result.value();
  CHECK(value == 42);
}
