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

#include <catch2/catch_all.hpp>
#include <functional>

#include <exec/sequence_senders.hpp>
#include <stdexec/execution.hpp>

#include "sio/sequence/first.hpp"
#include "sio/sequence/fork.hpp"
#include "sio/sequence/last.hpp"
#include "sio/sequence/reduce.hpp"
#include "sio/sequence/zip.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/then_each.hpp"

TEST_CASE("zip - with just connects with first", "[zip][first]") {
  auto sequence = sio::zip(stdexec::just(42));
  auto first = sio::first(sequence);
  auto [v] = stdexec::sync_wait(first).value();
  CHECK(v == 42);
}

TEST_CASE("zip - with two justs connects with first", "[zip][first]") {
  auto sequence = sio::zip(stdexec::just(42), stdexec::just(43));
  auto first = sio::first(sequence);
  auto [v, w] = stdexec::sync_wait(first).value();
  CHECK(v == 42);
  CHECK(w == 43);
}

TEST_CASE("zip - array with sender", "[zip][iterate]") {
  std::array<int, 2> array{42, 43};
  int count = 0;
  auto sequence = sio::zip(stdexec::just(42), sio::iterate(std::ranges::views::all(array)))
                | sio::first() //
                | sio::then_each([&](int v, int w) {
                    ++count;
                    CHECK(v == 42);
                    CHECK(w == 42);
                  });
  stdexec::sync_wait(sio::ignore_all(sequence));
  CHECK(count == 1);
}

TEST_CASE("zip - array with array", "[zip][iterate]") {
  std::array<int, 3> array{42, 43, 44};
  int count = 0;
  auto sequence =
    sio::zip(sio::iterate(std::views::all(array)), sio::iterate(std::views::all(array))) //
    | sio::then_each([&](int v, int w) {
        CHECK(v == 42 + count);
        CHECK(v == w);
        ++count;
      }) //
    | sio::ignore_all();
  stdexec::sync_wait(std::move(sequence));
  CHECK(count == 3);
}

// TEST_CASE("zip - a compilcated case", "[zip][iterate][fork]") {
//   std::array<int, 2> array{42, 43};
//   int count = 0;
//
//   auto reduce = sio::reduce(sio::iterate(std::views::all(array)), 1.0, std::multiplies<float>());
//   auto sequence =
//     sio::zip(stdexec::just(42), sio::iterate(std::views::all(array)), std::move(reduce)) //
//     | sio::fork()                                                                        //
//     | sio::first()                                                                       //
//     | sio::then_each([&](int v, int w, float m) {
//         ++count;
//         CHECK(v == 42);
//         CHECK(w == 42);
//         CHECK(m == 42 * 43.0);
//       });
//   stdexec::sync_wait(sio::last(sequence));
//   CHECK(count == 1);
// }
