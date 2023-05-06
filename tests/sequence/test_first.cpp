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
#include "sio/first.hpp"

#include "sio/empty_sequence.hpp"

#include <catch2/catch.hpp>

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

// TEST_CASE("first - with empty_sequence", "[sequence][first]") {
  // auto first = sio::first(sio::empty_sequence());
  // REQUIRE_FALSE(stdexec::sync_wait(first));
// }