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

#include "sio/iterate.hpp"

#include "sio/first.hpp"

#include <catch2/catch.hpp>

TEST_CASE("iterate - with just sender", "[sequence][iterate][first]") {
  std::array<int, 1> arr{42};
  auto iterate = sio::first(sio::iterate(arr));
  auto [front] = stdexec::sync_wait(iterate).value();
  REQUIRE(front == 42);
}

TEST_CASE("iterate - with just sender and back binder", "[sequence][iterate][first]") {
  std::array<int, 1> arr{42};
  auto iterate = sio::iterate(arr) | sio::first();
  auto [front] = stdexec::sync_wait(iterate).value();
  REQUIRE(front == 42);
}