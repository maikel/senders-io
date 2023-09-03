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
#include "sio/sequence/scan.hpp"

#include "sio/sequence/empty_sequence.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/first.hpp"
#include "sio/sequence/last.hpp"

#include <catch2/catch.hpp>

TEST_CASE("scan - with just sender", "[sequence][scan][first]") {
  auto first = sio::first(sio::scan(stdexec::just(42), 0));
  auto [x] = stdexec::sync_wait(first).value();
  REQUIRE(x == 42);
}

TEST_CASE("scan - with just sender and back binder", "[sequence][scan][last]") {
  auto f = sio::scan(stdexec::just(41), 1) | sio::last();
  auto [x] = stdexec::sync_wait(f).value();
  REQUIRE(x == 42);
}

TEST_CASE("scan - with iterate", "[sequence][scan][last][iterate]") {
  std::array<int, 3> arr{1, 2, 3};
  auto iterate = sio::iterate(std::ranges::views::all(arr));
  STATIC_REQUIRE(exec::sequence_sender<decltype(iterate)>);
  auto scan = sio::scan(sio::iterate(std::ranges::views::all(arr)), 0);
  using Scan = decltype(scan);
  // stdexec::__types<Scan> {};
  // using sigs = decltype(stdexec::get_completion_signatures(scan, stdexec::empty_env{}));
  STATIC_REQUIRE(stdexec::sender_in<Scan, stdexec::empty_env>);
  STATIC_REQUIRE(exec::sequence_sender<Scan>);
  // using sigs = stdexec::completion_signatures_of_t<decltype(scan), stdexec::empty_env>;
  // stdexec::__types<sigs> {};
  // STATIC_REQUIRE(stdexec::sender<Scan>);
  auto [x] = stdexec::sync_wait(sio::last(scan)).value();
  REQUIRE(x == 6);
}
