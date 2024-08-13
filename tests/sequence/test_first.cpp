/*
 * Copyright (c) 2024 Maikel Nadolski
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

#include "sio/sequence/iterate.hpp"
#include "sio/sequence/first.hpp"

#include <catch2/catch_all.hpp>

#include <stdexec/execution.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/sequence/empty_sequence.hpp>
#include <exec/sequence/ignore_all_values.hpp>

TEST_CASE("first - with just sender", "[sequence][first]") {
  auto first = sio::first(stdexec::just(42));
  auto [x] = stdexec::sync_wait(first).value();
  REQUIRE(x == 42);
}

TEST_CASE("first - with just sender and back binder", "[sequence][first]") {
  auto f = stdexec::just(42) | sio::first();
  auto [x] = stdexec::sync_wait(f).value();
  REQUIRE(x == 42);
}

TEST_CASE("first - with self", "[sequence][first]") {
  auto first = sio::first(sio::first(sio::first(stdexec::just(42))));
  auto [x] = stdexec::sync_wait(first).value();
  REQUIRE(x == 42);
}

TEST_CASE("first - complicated case", "[sequence][first]") {
  std::array arr{1, 2, 3};
  stdexec::sender auto sndr =
    sio::iterate(std::views::all(arr)) //
    | sio::first()                     //
    | exec::transform_each(stdexec::then([](int t) noexcept { REQUIRE(t == 1); }))
    | exec::ignore_all_values();
  stdexec::sync_wait(std::move(sndr));
}

TEST_CASE("first - with ranges", "[sequence][first]") {
  auto f = sio::iterate(std::vector{1, 2, 3}) | sio::first();
  auto [x] = stdexec::sync_wait(f).value();
  REQUIRE(x == 1);
}

// TODO: need to add item_types for empty_sequence
// TEST_CASE("first - with empty_sequence", "[sequence][first]") {
//   auto first = sio::first(exec::empty_sequence());
//   REQUIRE_FALSE(stdexec::sync_wait(std::move(first)));
// }
