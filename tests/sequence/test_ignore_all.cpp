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

#include "sio/sequence/ignore_all.hpp"

#include "sio/sequence/iterate.hpp"
#include "sio/sequence/then_each.hpp"

#include <catch2/catch_all.hpp>

TEST_CASE("ignore_all - with just sender", "[sequence][ignore_all]") {
  auto ignore = sio::ignore_all(stdexec::just(42));
  using ignore_t = decltype(ignore);
  using completion_sigs = stdexec::completion_signatures_of_t<ignore_t, stdexec::empty_env>;
  STATIC_REQUIRE(
    stdexec::same_as<completion_sigs, stdexec::completion_signatures<stdexec::set_value_t()>>);
  REQUIRE(stdexec::sync_wait(ignore));
}

TEST_CASE("ignore_all - with just_stopped sender", "[sequence][ignore_all]") {
  auto ignore = sio::ignore_all(stdexec::just_stopped());
  using ignore_t = decltype(ignore);
  using completion_sigs = stdexec::completion_signatures_of_t<ignore_t, stdexec::empty_env>;
  STATIC_REQUIRE(stdexec::same_as<
                 completion_sigs,
                 stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>>);
  REQUIRE(!stdexec::sync_wait(ignore));
}

TEST_CASE("ignore_all - with just_error sender", "[sequence][ignore_all]") {
  auto ignore = sio::ignore_all(stdexec::just_error(42));
  using ignore_t = decltype(ignore);
  using completion_sigs = stdexec::completion_signatures_of_t<ignore_t, stdexec::empty_env>;
  STATIC_REQUIRE(
    stdexec::same_as<
      completion_sigs,
      stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_error_t(int)>>);
  REQUIRE_THROWS(stdexec::sync_wait(ignore));
}

TEST_CASE("ignore_all - with iterate", "[sequence][ignore_all][iterate]") {
  std::array<int, 2> array{42, 42};
  auto iter = sio::iterate(array) | sio::ignore_all();
  REQUIRE(stdexec::sync_wait(iter));
}

TEST_CASE("ignore_all - with iterate and then_each", "[sequence][ignore_all][iterate][then_each]") {
  std::array<int, 2> array{42, 43};
  int count = 0;
  auto iter = sio::iterate(array)                                        //
            | sio::then_each([&](int v) { REQUIRE(v == 42 + count++); }) //
            | sio::ignore_all();
  REQUIRE(stdexec::sync_wait(iter));
  REQUIRE(count == 2);
}