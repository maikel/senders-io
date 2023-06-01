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

struct Token {
  auto close(sio::close_t) const noexcept { 
    return stdexec::just();
  }
};

struct Resource {
  auto open(sio::open_t) const noexcept {
    return stdexec::just(Token{});
  }
};

TEST_CASE("async_resource - sequence", "[async_resource]") {
  STATIC_REQUIRE(sio::with_open_and_close<Resource, stdexec::empty_env>);
  Resource res{};
  auto seq = sio::run(res);
  using sequence_t = decltype(seq);
  STATIC_REQUIRE(exec::sequence_sender_in<sequence_t, stdexec::empty_env>);
  STATIC_REQUIRE(exec::sequence_sender_to<sequence_t, any_receiver>);
  auto op = exec::subscribe(seq, any_receiver{});
  stdexec::start(op);
}

TEST_CASE("async_resource - use_resources", "[async_resource]") {
  Resource res{};
  auto sndr = sio::use_resources([](Token) {
    return stdexec::just(42);
  }, res);
  auto result = stdexec::sync_wait(sndr);
  CHECK(result);
  auto [value] = result.value();
  CHECK(value == 42);
}