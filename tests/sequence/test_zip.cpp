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

#include "sio/sequence/zip.hpp"

#include "sio/sequence/first.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/ignore_all.hpp"
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

TEST_CASE("zip - with just connects with any_receiver", "[zip]") {
  auto sender = sio::zip(stdexec::just(42));
  using zip_t = decltype(sender);
  STATIC_REQUIRE(stdexec::sender<zip_t>);
  STATIC_REQUIRE(exec::sequence_sender_in<zip_t, stdexec::empty_env>);
  STATIC_REQUIRE(stdexec::receiver<any_receiver>);
  STATIC_REQUIRE_FALSE(stdexec::sender_to<zip_t, any_receiver>);
  using compls = stdexec::completion_signatures_of_t<zip_t, stdexec::empty_env>;
  using seqs = exec::__sequence_completion_signatures_of_t<zip_t, stdexec::empty_env>;
  STATIC_REQUIRE(stdexec::receiver_of<any_receiver, seqs>);
  STATIC_REQUIRE(exec::sequence_receiver_of<any_receiver, compls>);
  STATIC_REQUIRE(exec::sequence_receiver_from<any_receiver, zip_t>);
  STATIC_REQUIRE(exec::sequence_sender_to<zip_t, any_receiver>);
  auto op = exec::subscribe(sender, any_receiver{});
  stdexec::start(op);
}

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
  auto sequence = sio::zip(stdexec::just(42), sio::iterate(array)) //
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
  auto sequence = sio::zip(sio::iterate(array), sio::iterate(array)) //
                | sio::then_each([&](int v, int w) {
                    CHECK(v == 42 + count);
                    CHECK(v == w);
                    ++count;
                  });
  stdexec::sync_wait(sio::ignore_all(sequence));
  CHECK(count == 3);
}