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

#include "sio/sequence/let_value_each.hpp"
#include "sio/sequence/transform_each.hpp"
#include "sio/sequence/then_each.hpp"
#include "sio/sequence/first.hpp"
#include "sio/sequence/iterate.hpp"

#include <catch2/catch_all.hpp>

TEST_CASE("transform_each - with just sender", "[sequence][transform_each]") {
  auto successor = sio::first(
    sio::transform_each(stdexec::just(41), stdexec::then([](int x) { return x + 1; })));
  auto [x] = stdexec::sync_wait(successor).value();
  REQUIRE(x == 42);
}

TEST_CASE("transform_each - with iterate", "[sequence][transform_each][iterate]") {
  std::array<int, 2> array{41, 41};
  auto successor = sio::first(
    sio::transform_each(sio::iterate(array), stdexec::then([](int x) { return x + 1; })));
  auto [x] = stdexec::sync_wait(successor).value();
  REQUIRE(x == 42);
}

TEST_CASE("transform_each - with iterate and binder back", "[sequence][transform_each][iterate]") {
  std::array<int, 2> array{41, 41};
  auto successor = sio::iterate(array) |                                             //
                   sio::transform_each(stdexec::then([](int x) { return x + 1; })) | //
                   sio::first();
  auto [x] = stdexec::sync_wait(successor).value();
  REQUIRE(x == 42);
}

TEST_CASE("then_each - with iterate and binder back", "[sequence][transform_each][iterate]") {
  std::array<int, 2> array{41, 41};
  auto successor = sio::iterate(array) |                         //
                   sio::then_each([](int x) { return x + 1; }) | //
                   sio::first();
  auto [x] = stdexec::sync_wait(successor).value();
  REQUIRE(x == 42);
}

TEST_CASE("let_value_each - with iterate and binder back", "[sequence][transform_each][iterate]") {
  std::array<int, 2> array{41, 41};
  auto successor = sio::iterate(array) |                                             //
                   sio::let_value_each([](int x) { return stdexec::just(x + 1); }) | //
                   sio::first();
  auto [x] = stdexec::sync_wait(successor).value();
  REQUIRE(x == 42);
}
