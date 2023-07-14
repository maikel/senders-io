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


#include "sio/sequence/merge_each.hpp"

#include "sio/sequence/first.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/then_each.hpp"

#include <catch2/catch.hpp>

struct any_receiver {
  template <class Sender>
  auto set_next(exec::set_next_t, Sender&&) noexcept {
    return stdexec::just();
  }

  void set_value(stdexec::set_value_t) && noexcept {
  }

  void set_stopped(stdexec::set_stopped_t) && noexcept {
  }

  template <class E>
  void set_error(stdexec::set_error_t, E&&) && noexcept {
  }

  stdexec::empty_env get_env(stdexec::get_env_t) const noexcept {
    return {};
  }
};

TEST_CASE("merge_each - just", "[merge_each]") {
  auto merge = sio::merge_each(stdexec::just(42));
  using merge_t = decltype(merge);
  using env = stdexec::empty_env;
  STATIC_REQUIRE(stdexec::sender_in<merge_t, env>);
  STATIC_REQUIRE(exec::sequence_sender_in<merge_t, env>);
  STATIC_REQUIRE(exec::sequence_sender_to<merge_t, any_receiver>);
  auto op = exec::subscribe(merge, any_receiver{});
  stdexec::start(op);
}

TEST_CASE("merge_each - just and first", "[merge_each][first]") {
  auto merge = sio::merge_each(stdexec::just(42));
  auto first = sio::first(merge);
  auto [v] = stdexec::sync_wait(first).value();
  CHECK(v == 42);
}

TEST_CASE("merge_each - two senders count with ignore_all", "[merge_each][ignore_all][then_each]") {
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
  "[merge_each][ignore_all][then_each][iterate]") {
  std::array<int, 2> arr{42, 42};
  int count = 0;
  auto merge = sio::merge_each(stdexec::just(42), sio::iterate(arr)) //
             | sio::then_each([&](int value) {
                 ++count;
                 CHECK(value == 42);
               });
  CHECK(stdexec::sync_wait(sio::ignore_all(merge)));
  CHECK(count == 3);
}

TEST_CASE("merge_each - for subsequences", "[merge_each][then_each][iterate]") {
  std::array<int, 2> indices{1, 2};
  int counter = 0;
  auto merge = sio::iterate(indices) //
             | sio::then_each([](int i) {
                 return sio::iterate(std::array<int, 2>{i, i});
               })                //
             | sio::merge_each()
             | sio::then_each([&](int value) {
                 if (counter == 0)
                   CHECK(value == 1);
                 else if (counter == 1)
                   CHECK(value == 1);
                 else if (counter == 2)
                   CHECK(value == 2);
                 else if (counter == 3)
                   CHECK(value == 2);
                 else
                   CHECK(false);
                 ++counter;
               });
  CHECK(stdexec::sync_wait(sio::ignore_all(merge)));
}