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
#include <sio/async_mutex.hpp>

#include <catch2/catch.hpp>

TEST_CASE("async_mutex - lock is a sender", "[async_mutex]") {
  sio::async_mutex mutex{};
  bool check = false;
  stdexec::sync_wait(mutex.lock() | stdexec::then([&] { check = true; }));
  CHECK(check);
}