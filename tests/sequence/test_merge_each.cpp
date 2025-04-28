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
#include "sio/sequence/merge_each.hpp"
#include "sio/sequence/first.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/then_each.hpp"

#include "common/test_receiver.hpp"

#include <catch2/catch_all.hpp>

#include <ranges>

TEST_CASE("merge_each - just", "[sio][merge_each]") {
  auto merge = sio::merge_each(stdexec::just(42));
  using merge_t = decltype(merge);
  using env = stdexec::env<>;
  STATIC_REQUIRE(stdexec::sender_in<merge_t, env>);
  STATIC_REQUIRE(exec::sequence_sender_in<merge_t, env>);
  STATIC_REQUIRE(exec::sequence_sender_to<merge_t, any_sequence_receiver>);
  auto op = exec::subscribe(merge, any_sequence_receiver{});
  stdexec::start(op);
}

TEST_CASE("merge_each - just and first", "[sio][merge_each][first]") {
  auto merge = sio::merge_each(stdexec::just(42));
  auto first = sio::first(merge);
  auto [v] = stdexec::sync_wait(first).value();
  CHECK(v == 42);
}

TEST_CASE(
  "merge_each - two senders count with ignore_all",
  "[sio][merge_each][ignore_all][then_each]") {
  int count = 0;
  auto merge = sio::merge_each(stdexec::just(42), stdexec::just(42)) //
             | sio::then_each([&](int value) {
                 ++count;
                 CHECK(value == 42);
               });
  CHECK(stdexec::sync_wait(sio::ignore_all(merge)));
  CHECK(count == 2);
}

TEST_CASE(
  "merge_each - iterate and senders count with ignore_all",
  "[sio][merge_each][ignore_all][then_each][iterate]") {
  std::array<int, 2> arr{42, 42};
  int count = 0;
  auto merge = sio::merge_each(stdexec::just(42), sio::iterate(std::views::all(arr))) //
             | sio::then_each([&](int value) {
                 ++count;
                 CHECK(value == 42);
               });
  CHECK(stdexec::sync_wait(sio::ignore_all(merge)));
  CHECK(count == 3);
}

TEST_CASE(
  "merge_each - test merge_each accepts only an iterate sender",
  "[sio][merge_each][iterate][first]") {
  std::array<int, 2> indices{1, 2};
  auto sndr = sio::iterate(std::views::all(indices)) //
            | sio::merge_each()                      //
            | sio::first();
  auto [v] = stdexec::sync_wait(std::move(sndr)).value();
  CHECK(v == 1);
}

TEST_CASE(
  "merge_each - test merge_each accepts two iterate senders",
  "[sio][merge_each][iterate][first]") {
  std::array<int, 2> indices{1, 2};
  auto sndr = sio::merge_each(
                sio::iterate(std::views::all(indices)),
                sio::iterate(std::views::all(indices))) //
            | sio::first();
  auto [v] = stdexec::sync_wait(std::move(sndr)).value();
  CHECK(v == 1);
}
