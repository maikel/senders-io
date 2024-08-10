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

#include <catch2/catch_all.hpp>
#include <exec/sequence_senders.hpp>
#include <stdexec/execution.hpp>

#include "sio/sequence/fork.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/scan.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/first.hpp"
#include "sio/sequence/last.hpp"

TEST_CASE("scan - with just sender and ignore_all back binder", "[sequence][scan][ignore_all]") {
  auto f = sio::scan(stdexec::just(41), 1) | sio::ignore_all();
  stdexec::sync_wait(f);
}

TEST_CASE("scan - with just sender and back binder", "[sequence][scan][last]") {
  auto f = sio::scan(stdexec::just(41), 1) | sio::last();
  auto [x] = stdexec::sync_wait(f).value();
  REQUIRE(x == 42);
}

TEST_CASE("scan - with just sender and first back binder", "[sequence][scan][first]") {
  auto f = sio::scan(stdexec::just(41), 1) | sio::first();
  auto [x] = stdexec::sync_wait(f).value();
  REQUIRE(x == 42);
}

TEST_CASE("scan - with just sender and fork", "[sequence][scan][first][fork]") {
  auto f = sio::scan(stdexec::just(41), 1) | sio::fork() | sio::first();
  auto [x] = stdexec::sync_wait(f).value();
  REQUIRE(x == 42);
}

TEST_CASE("scan - with iterate", "[sequence][scan][last][iterate]") {
  std::array<int, 3> arr{1, 2, 3};
  auto sndr = sio::scan(sio::iterate(std::ranges::views::all(arr)), 0) //
            | sio::last();
  auto [x] = stdexec::sync_wait(sndr).value();
  REQUIRE(x == 6);
}

TEST_CASE("scan - with multiply function", "[sequence][scan][last][iterate]") {
  std::array<int, 3> arr{1, 2, 3};
  auto sndr = sio::scan(sio::iterate(std::ranges::views::all(arr)), 1, std::multiplies<int>()) //
            | sio::last();
  auto [x] = stdexec::sync_wait(sndr).value();
  REQUIRE(x == 6);
}
