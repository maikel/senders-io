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
#include <sio/async_channel.hpp>
#include <sio/sequence/first.hpp>
#include <catch2/catch.hpp>

TEST_CASE("async_channel - just", "[async_channel]") {
  sio::async_channel channel{};
  bool is_read = false;
  auto use = sio::async::use_resources([&](sio::async_channel_handle handle) {
    auto read = handle.subscribe() //
              | sio::first() //
              | stdexec::then([&]() noexcept {
                  is_read = true;
                });
    auto write = handle.notify_all();
    return stdexec::when_all(read, write);
  }, channel);

  stdexec::sync_wait(use);

  CHECK(is_read);
}