/*
 * Copyright (c) 2024 Xiaoming Zhang
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
#include <netinet/in.h>
#include <sys/socket.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include <catch2/catch_all.hpp>

#include "sio/ip/address.hpp"
#include "sio/ip/endpoint.hpp"

using namespace sio::ip;

TEST_CASE("[construct endpoint with family and port should return any address]", "[endpoint]") {
  endpoint ep_v4{AF_INET, 80};
  CHECK(ep_v4.address().is_v4());
  CHECK(ep_v4.address().is_unspecified());
  CHECK(ep_v4.port() == 80);

  endpoint ep_v6{AF_INET6, 80};
  CHECK(ep_v6.address().is_v6());
  CHECK(ep_v6.address().is_unspecified());
  CHECK(ep_v6.port() == 80);
}

TEST_CASE("[construct endpoint with specific address and port]", "[endpoint]") {
  address_v4 addr4 = make_address_v4("127.0.0.1");
  endpoint endpoint4{addr4, 80};
  CHECK(endpoint4.address().is_v4());
  CHECK(endpoint4.address().to_v4().is_loopback());
  CHECK(endpoint4.port() == 80);

  address_v6 addr6 = make_address_v6("::ffff:1.1.1.1");
  endpoint endpoint6{addr6, 80};
  CHECK(endpoint6.address().is_v6());
  CHECK(endpoint6.address().to_v6().is_v4_mapped());
  CHECK(endpoint6.port() == 80);
}

TEST_CASE("[copy constructor should work]", "[endpoint]") {
  endpoint ep{address_v4::any(), 80};
  endpoint ep_copy{ep};
  CHECK(ep == ep_copy);
  CHECK(ep_copy.port() == 80);
}

TEST_CASE("[comparision should work]", "[endpoint]") {
  endpoint ep0{make_address_v6("::ffff:1.1.1.1"), 80};
  endpoint ep1{make_address_v6("::ffff:1.1.1.1"), 80};
  endpoint ep2{make_address_v6("::ffff:2.2.2.2"), 79};
  endpoint ep3{make_address_v6("::ffff:2.2.2.2"), 78};

  CHECK(ep0 == ep1);
  CHECK(ep1 < ep2);
  CHECK(ep2 > ep3);
}

TEST_CASE("hash endpoint", "endpoint.hash") {
  endpoint ep{make_address_v6("::ffff:1.1.1.1"), 80};
  std::unordered_map<endpoint, bool> table;
  table[ep] = false;
  CHECK(table[ep] == false);
  table[ep] = true;
  CHECK(table[ep] == true);
}
