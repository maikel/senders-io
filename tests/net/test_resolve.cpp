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
#include "sio/net/ip/resolve.hpp"
#include "sio/net/ip/tcp.hpp"
#include "sio/sequence/first.hpp"

#include <catch2/catch.hpp>

TEST_CASE("async::resolve - Resolve ipv4 localhost", "[net][resolve][first]") {
  auto sndr = sio::first(sio::async::resolve(sio::net::ip::tcp::v4(), "localhost", "http"));
  auto result = stdexec::sync_wait(sndr);
  CHECK(result);
  auto [response] = result.value();
  CHECK(response.endpoint().address().is_v4());
  std::string str = response.endpoint().address().to_string();
  CHECK(str == "127.0.0.1");
}

TEST_CASE("async::resolve - Resolve ipv6 localhost", "[net][resolve][first]") {
  auto sndr = sio::first(sio::async::resolve(sio::net::ip::tcp::v6(), "localhost", "80"));
  auto result = stdexec::sync_wait(sndr);
  CHECK(result);
  auto [response] = result.value();
  CHECK(response.endpoint().address().is_v6());
  std::string str = response.endpoint().address().to_string();
  CHECK(str == "::1");
}