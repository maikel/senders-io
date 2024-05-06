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
#include <catch2/catch.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/sequence/empty_sequence.hpp>
#include <exec/sequence/ignore_all_values.hpp>

#include "sio/sequence/last.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/first.hpp"

TEST_CASE("last - with just sender", "[sequence][last]") {
  auto last = sio::last(stdexec::just(42));
  auto [x] = stdexec::sync_wait(last).value();
  REQUIRE(x == 42);
}

TEST_CASE("last - with just sender and back binder", "[sequence][last]") {
  auto f = stdexec::just(42) | sio::last();
  auto [x] = stdexec::sync_wait(f).value();
  REQUIRE(x == 42);
}

TEST_CASE("last - with self", "[sequence][last]") {
  auto first = sio::last(sio::last(sio::last(stdexec::just(42))));
  auto [x] = stdexec::sync_wait(first).value();
  REQUIRE(x == 42);
}

TEST_CASE("last - with iterate sender", "[sequence][last][iterate]") {
  std::array<int, 3> arr{1, 2, 3};
  auto last = sio::last(sio::iterate(arr));
  auto [x] = stdexec::sync_wait(last).value();
  REQUIRE(x == 3);
}

TEST_CASE("last - with first sender", "[sequence][last][first]") {
  std::array<int, 3> arr{1, 2, 3};
  auto first = sio::first(sio::last(sio::iterate(arr)));
  auto [x] = stdexec::sync_wait(first).value();
  REQUIRE(x == 3);
}

TEST_CASE("last - complicated case", "[sequence][last]") {
  std::array arr{1, 2, 3};
  stdexec::sender auto sndr =
    sio::iterate(std::views::all(arr)) //
    | sio::last()                      //
    | exec::transform_each(stdexec::then([](int t) noexcept { REQUIRE(t == 3); }))
    | exec::ignore_all_values();
  stdexec::sync_wait(std::move(sndr));
}

TEST_CASE("last - with empty ranges", "[sequence][last]") {
  auto f = sio::iterate(std::vector<int>{}) | sio::last();
  stdexec::sync_wait(f).value();
}

// TEST_CASE("last - with empty_sequence", "[sequence][last]") {
// auto last = sio::last(sio::empty_sequence());
// REQUIRE_FALSE(stdexec::sync_wait(last));
// }
