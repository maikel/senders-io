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

#include <stdexec/execution.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/sequence/empty_sequence.hpp>
#include <exec/sequence/ignore_all_values.hpp>
#include <catch2/catch.hpp>

#include "common/test_receiver.hpp"
#include "common/test_sequence_senders.hpp"

#include "sio/sequence/iterate.hpp"
#include "sio/sequence/first.hpp"
#include "sio/sequence/sequence_concepts.hpp"

TEST_CASE("first - with iterate_1_to_5", "[sequence][first]") {
  using env = stdexec::empty_env;

  auto sndr = sio::first(iterate_1_to_5{});
  using first_t = decltype(sndr);
  static_assert(stdexec::enable_sender<first_t>);

  // Check first_t has valid completion sigatures.
  using sigs = stdexec::completion_signatures_of_t<first_t, env>;
  using golden_sigs = stdexec::completion_signatures<
    stdexec::set_value_t(int),
    stdexec::set_error_t(eptr),
    stdexec::set_stopped_t()>;
  static_assert(set_equivalent<sigs, golden_sigs>);

  static_assert(stdexec::sender_to<first_t, any_receiver>);

  // Check first_t has valid item types.
  using item_type = exec::item_types_of_t<first_t, env>;
  static_assert(exec::valid_item_types<item_type>);

  // Check first_t has valid item completion sigatures.
  using item_sigs = exec::item_completion_signatures_of_t<first_t, env>;
  using golden_item_sigs = stdexec::completion_signatures<
    stdexec::set_value_t(int),
    stdexec::set_error_t(eptr),
    stdexec::set_stopped_t()>;
  static_assert(set_equivalent<item_sigs, golden_item_sigs>);

  using iter_item_sender = iterate_1_to_5_::item_sender;
  using result_variant_t = sio::first_::traits<iter_item_sender, env>::result_variant_t;
  using first_item_sender = sio::first_::item_sender<iter_item_sender, result_variant_t, false>;
  using iter_next_receiver = iterate_1_to_5_::next_receiver<any_receiver>;
  using first_item_receiver =
    sio::first_::item_receiver< iter_next_receiver, result_variant_t, false>;
  static_assert(stdexec::sender_to<iter_item_sender, first_item_receiver>);
  static_assert(stdexec::sender_to<first_item_sender, iter_next_receiver>);

  using iter_op_t = stdexec::connect_result_t< iter_item_sender, first_item_receiver>;
  static_assert(stdexec::operation_state<iter_op_t>);

  using first_op_t = stdexec::connect_result_t< first_item_sender, iter_next_receiver>;
  static_assert(stdexec::operation_state<first_op_t>);

  auto op = stdexec::connect(std::move(sndr), any_receiver{});
  stdexec::start(op);
}

TEST_CASE("first - with iterate_1_to_5 sender", "[sequence][first]") {
  auto first = sio::first(iterate_1_to_5_::iterate_1_to_5{});
  auto [x] = stdexec::sync_wait(std::move(first)).value();
  REQUIRE(x == 1);
}

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

// TODO: need to fix empty_sequence item_types
// TEST_CASE("first - with empty_sequence", "[sequence][first]") {
//   auto first = sio::first(exec::empty_sequence());
//   REQUIRE_FALSE(stdexec::sync_wait(std::move(first)));
// }
