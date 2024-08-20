/*
 * Copyright (c) 2024 Emmett Zhang
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
#include <array>

#include <catch2/catch_all.hpp>

#include "sio/ip/address.hpp"

using namespace sio::ip;

// Used for test IPv4 address
static const std::vector<std::string>
  str_ip{"0.0.0.0", "127.0.0.1", "224.0.0.0", "120.121.122.123"};
static const std::vector<uint32_t> uint_ip{0, 2130706433, 3758096384, 2021227131};


// Used for test IPv6 address
static constexpr ::in6_addr unspecified_data{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const auto unspecified = std::bit_cast<address_v6::bytes_type>(unspecified_data);

static constexpr ::in6_addr loopback_data{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
static const auto loopback = std::bit_cast<address_v6::bytes_type>(loopback_data);

static constexpr ::in6_addr
  link_local_data{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x12, 0x13, 0x14};
static const auto link_local = std::bit_cast<address_v6::bytes_type>(link_local_data);

static constexpr ::in6_addr
  site_local_data{0xfe, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x12, 0x13, 0x14};
static const auto site_local = std::bit_cast<address_v6::bytes_type>(site_local_data);

static constexpr ::in6_addr
  v4_mapped_data{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0xfe, 0xff};
static const auto v4_mapped = std::bit_cast<address_v6::bytes_type>(v4_mapped_data);

static constexpr ::in6_addr
  multicast_data{0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14};
static const auto multicast = std::bit_cast<address_v6::bytes_type>(multicast_data);

static constexpr ::in6_addr
  multicast_global_data{0xff, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14};
static const auto multicast_global = std::bit_cast<address_v6::bytes_type>(multicast_global_data);

static constexpr ::in6_addr
  multicast_link_local_data{0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14};
static const auto multicast_link_local = std::bit_cast<address_v6::bytes_type>(
  multicast_link_local_data);

static constexpr ::in6_addr
  multicast_node_local_data{0xff, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14};
static const auto multicast_node_local = std::bit_cast<address_v6::bytes_type>(
  multicast_node_local_data);

static constexpr ::in6_addr
  multicast_org_local_data{0xff, 0x08, 0, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14};
static const auto multicast_org_local = std::bit_cast<address_v6::bytes_type>(
  multicast_org_local_data);

static constexpr ::in6_addr
  multicast_site_local_data{0xff, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14};
static const auto multicast_site_local = std::bit_cast<address_v6::bytes_type>(
  multicast_site_local_data);

TEST_CASE("[construct an IPv4-address from byte address]", "[address_v4.ctor]") {
  unsigned char unspecified_data[] = {0, 0, 0, 0};
  address_v4 unspecified{std::bit_cast<address_v4::bytes_type>(unspecified_data)};
  CHECK(unspecified.to_uint() == uint_ip[0]);
  CHECK(unspecified.to_string() == "0.0.0.0");
  CHECK(unspecified.is_unspecified());

  unsigned char loopback_data[] = {127, 0, 0, 1};
  address_v4 loopback{std::bit_cast<address_v4::bytes_type>(loopback_data)};
  CHECK(loopback.to_uint() == uint_ip[1]);
  CHECK(loopback.to_string() == "127.0.0.1");
  CHECK(loopback.is_loopback());

  unsigned char multicast_data[] = {224, 0, 0, 0};
  address_v4 multicast{std::bit_cast<address_v4::bytes_type>(multicast_data)};
  CHECK(multicast.to_uint() == uint_ip[2]);
  CHECK(multicast.to_string() == "224.0.0.0");
  CHECK(multicast.is_multicast());

  unsigned char other_data[] = {120, 121, 122, 123};
  address_v4 other{std::bit_cast<address_v4::bytes_type>(other_data)};
  CHECK(other.to_uint() == uint_ip[3]);
  CHECK(other.to_string() == "120.121.122.123");
  CHECK((!other.is_loopback() && !other.is_multicast() && !other.is_unspecified()));
}

TEST_CASE("[construct an IPv4-address from integer address]", "[address_v4]") {
  unsigned char unspecified_data[] = {0, 0, 0, 0};
  address_v4 unspecified{uint_ip[0]};
  CHECK(unspecified.to_uint() == uint_ip[0]);
  CHECK(unspecified.to_bytes() == std::bit_cast<address_v4::bytes_type>(unspecified_data));
  CHECK(unspecified.to_string() == "0.0.0.0");
  CHECK(unspecified.is_unspecified());

  unsigned char loopback_data[] = {127, 0, 0, 1};
  address_v4 loopback{uint_ip[1]};
  CHECK(loopback.to_uint() == uint_ip[1]);
  CHECK(loopback.to_bytes() == std::bit_cast<address_v4::bytes_type>(loopback_data));
  CHECK(loopback.to_string() == "127.0.0.1");
  CHECK(loopback.is_loopback());

  unsigned char multicast_data[] = {224, 0, 0, 0};
  address_v4 multicast{uint_ip[2]};
  CHECK(multicast.to_uint() == uint_ip[2]);
  CHECK(multicast.to_bytes() == std::bit_cast<address_v4::bytes_type>(multicast_data));
  CHECK(multicast.to_string() == "224.0.0.0");
  CHECK(multicast.is_multicast());

  unsigned char other_data[] = {120, 121, 122, 123};
  address_v4 other{uint_ip[3]};
  CHECK(other.to_uint() == uint_ip[3]);
  CHECK(other.to_bytes() == std::bit_cast<address_v4::bytes_type>(other_data));
  CHECK(other.to_string() == "120.121.122.123");
  CHECK((!other.is_loopback() && !other.is_multicast() && !other.is_unspecified()));
}

TEST_CASE("[copy constructor of address_v4 should correctly copy others]", "[address_v4]") {
  address_v4 other{uint_ip[3]};
  address_v4 other_copy{other};
  CHECK(other_copy.to_uint() == uint_ip[3]);
  CHECK(other_copy.to_string() == "120.121.122.123");
  CHECK((!other_copy.is_loopback() && !other_copy.is_multicast() && !other_copy.is_unspecified()));
  CHECK(other == other_copy);
}

TEST_CASE("[compare IPv4 address should return correct result]", "[address_v4]") {
  CHECK(address_v4{111} == address_v4{111});
  CHECK(address_v4{111} < address_v4{112});
  CHECK(address_v4{111} > address_v4{110});
}

TEST_CASE("address_v4::loopback() should return loopback address", "[address_v4.loopback]") {
  CHECK(address_v4::loopback().is_loopback());
  CHECK(address_v4::loopback().to_string() == "127.0.0.1");
}

TEST_CASE("[pass well-formed address to make_address_v4 should work]", "[address_v4]") {
  CHECK(make_address_v4("127.0.0.1").is_loopback());

  address_v4 v4 = make_address_v4("127.0.0.1");
  CHECK(v4.to_string() == "127.0.0.1");

  v4 = make_address_v4("120.121.122.123");
  CHECK(v4.to_string() == "120.121.122.123");

  v4 = make_address_v4(std::string_view{"120.121.122.123"});
  CHECK(v4.to_string() == "120.121.122.123");

  v4 = make_address_v4(std::string{"120.121.122.123"});
  CHECK(v4.to_string() == "120.121.122.123");

  v4 = make_address_v4(uint_ip[3]);
  CHECK(v4.to_string() == "120.121.122.123");
}

TEST_CASE(
  "[pass ill-formed address to make_address_v4 should return unspecified]",
  "[address_v4]") {
  CHECK(make_address_v4("300.0.0.0").is_unspecified());
  CHECK(make_address_v4("300.0.0").is_unspecified());
  CHECK(make_address_v4("300.0.0.0.0").is_unspecified());
}

TEST_CASE("hash address_v4", "address_v4.hash") {
  std::unordered_map<address_v4, bool> addresses;
  addresses[make_address_v4("120.121.122.123")] = false;
  addresses[make_address_v4("1.1.1.1")] = false;
  CHECK(addresses[make_address_v4("120.121.122.123")] == false);
  CHECK(addresses[make_address_v4("1.1.1.1")] == false);

  addresses[make_address_v4("120.121.122.123")] = true;
  CHECK(addresses[make_address_v4("120.121.122.123")] == true);
  CHECK(addresses[make_address_v4("1.1.1.1")] == false);
  addresses[make_address_v4("1.1.1.1")] = true;
  CHECK(addresses[make_address_v4("1.1.1.1")] == true);
}

TEST_CASE("[construct an IPv6 address from bytes with different type]", "[address_v6]") {
  address_v6 unspecified_addr{unspecified};
  CHECK(unspecified_addr.to_bytes() == unspecified);
  CHECK(unspecified_addr.to_string() == "::");
  CHECK(unspecified_addr.is_unspecified());

  address_v6 loopback_addr{loopback};
  CHECK(loopback_addr.to_bytes() == loopback);
  CHECK(loopback_addr.to_string() == "::1");
  CHECK(loopback_addr.is_loopback());

  address_v6 link_local_addr{link_local};
  CHECK(link_local_addr.to_bytes() == link_local);
  CHECK(link_local_addr.to_string() == "fe80::1112:1314");
  CHECK(link_local_addr.is_link_local());

  address_v6 site_local_addr{site_local};
  CHECK(site_local_addr.to_bytes() == site_local);
  CHECK(site_local_addr.to_string() == "fec0::1112:1314");
  CHECK(site_local_addr.is_site_local());

  address_v6 v4_mapped_addr{v4_mapped};
  CHECK(v4_mapped_addr.to_bytes() == v4_mapped);
  CHECK(v4_mapped_addr.to_string() == "::ffff:0.0.254.255");
  CHECK(v4_mapped_addr.is_v4_mapped());

  address_v6 multicast_addr{multicast};
  CHECK(multicast_addr.to_bytes() == multicast);
  CHECK(multicast_addr.to_string() == "ff00::910:1112:1314");
  CHECK(multicast_addr.is_multicast());

  address_v6 multicast_global_addr{multicast_global};
  CHECK(multicast_global_addr.to_bytes() == multicast_global);
  CHECK(multicast_global_addr.to_string() == "ff0e::910:1112:1314");
  CHECK(multicast_global_addr.is_multicast_global());

  address_v6 multicast_local_addr{multicast_link_local};
  CHECK(multicast_local_addr.to_bytes() == multicast_link_local);
  CHECK(multicast_local_addr.to_string() == "ff02::910:1112:1314");
  CHECK(multicast_local_addr.is_multicast_link_local());

  address_v6 multicast_node_local_addr{multicast_node_local};
  CHECK(multicast_node_local_addr.to_bytes() == multicast_node_local);
  CHECK(multicast_node_local_addr.to_string() == "ff01::910:1112:1314");
  CHECK(multicast_node_local_addr.is_multicast_node_local());

  address_v6 multicast_org_local_addr{multicast_org_local};
  CHECK(multicast_org_local_addr.to_bytes() == multicast_org_local);
  CHECK(multicast_org_local_addr.to_string() == "ff08::910:1112:1314");
  CHECK(multicast_org_local_addr.is_multicast_org_local());

  address_v6 multicast_site_local_addr{multicast_site_local};
  CHECK(multicast_site_local_addr.to_bytes() == multicast_site_local);
  CHECK(multicast_site_local_addr.to_string() == "ff05::910:1112:1314");
  CHECK(multicast_site_local_addr.is_multicast_site_local());
}

TEST_CASE("[copy constructor of address_v6 should correctly copy others]", "[address_v6]") {
  address_v6 addr6{multicast};
  address_v6 addr6_copy{addr6};
  CHECK(addr6_copy.to_bytes() == multicast);
  CHECK(addr6_copy.to_string() == "ff00::910:1112:1314");
  CHECK(addr6 == addr6_copy);
}

TEST_CASE("[compare IPv6 address should return correct result]", "[address_v6]") {
  CHECK(address_v6{multicast} == address_v6{multicast});
  CHECK(address_v6{loopback} < address_v6{multicast});
  CHECK(address_v6{loopback} > address_v6{unspecified});
}

TEST_CASE("[pass well-formed address to make_address_v6 should work]", "[address_v6]") {
  CHECK(make_address_v6("::ffff:127.0.0.1").is_v4_mapped());
  CHECK(make_address_v6("::ffff:127.0.0.1").to_string() == "::ffff:127.0.0.1");

  CHECK(make_address_v6(std::string_view{"ff0e::910:1112:1314"}).is_multicast_global());
  CHECK(
    make_address_v6(std::string_view{"ff0e::910:1112:1314"}).to_string() == "ff0e::910:1112:1314");

  CHECK(make_address_v6(std::string{"fec0::1112:1314"}).is_site_local());
  CHECK(make_address_v6(std::string{"fec0::1112:1314"}).to_string() == "fec0::1112:1314");

  CHECK(make_address_v6(v4_mapped_t::v4_mapped, make_address_v4("127.0.0.1")).is_v4_mapped());
  CHECK(
    make_address_v6(v4_mapped_t::v4_mapped, make_address_v4("127.0.0.1")).to_string()
    == "::ffff:127.0.0.1");
}

TEST_CASE("[make IPv4 mapped address]", "[address_v6]") {
  address_v6 v4_mapped_addr{v4_mapped};
  CHECK(v4_mapped_addr.to_bytes() == v4_mapped);
  CHECK(v4_mapped_addr.to_string() == "::ffff:0.0.254.255");
  CHECK(v4_mapped_addr.is_v4_mapped());
  address_v4 v4 = make_address_v4(v4_mapped_t::v4_mapped, v4_mapped_addr);
  CHECK(v4.to_string() == "0.0.254.255");
}

TEST_CASE("[make IPv4 mapped address use ill-formed address]", "[address_v6]") {
  address_v6 not_v4_mapped_addr = make_address_v6("fec0::1112:1314");
  CHECK(!not_v4_mapped_addr.is_v4_mapped());
  address_v4 v4 = make_address_v4(v4_mapped_t::v4_mapped, not_v4_mapped_addr);
  CHECK(v4.is_unspecified());
}

TEST_CASE(
  "[make_address_v6 with if-name should correctly set scope id]",
  "[address_v6.make_address_v6]") {
  constexpr auto get_all_if_names = []() -> std::vector<std::string> {
    std::vector<std::string> result;
    struct if_nameindex* idx = nullptr;
    struct if_nameindex* if_idx_start = if_nameindex();
    if (if_idx_start != NULL) {
      for (idx = if_idx_start; idx->if_index != 0 || idx->if_name != NULL; idx++) {
        result.emplace_back(idx->if_name);
      }
      if_freenameindex(if_idx_start);
    }
    return result;
  };

  // link local
  for (const auto& if_name: get_all_if_names()) {
    address_v6 v6 = make_address_v6(std::string("fe80::1112:1314%" + if_name));
    CHECK(v6.is_link_local());
    CHECK(v6.scope_id() == if_nametoindex(if_name.data()));
  }

  // multicast link local
  for (const auto& if_name: get_all_if_names()) {
    address_v6 v6 = make_address_v6(std::string("ff02::910:1112:1314%" + if_name));
    CHECK(v6.is_multicast_link_local());
    CHECK(v6.scope_id() == if_nametoindex(if_name.data()));
  }

  // others
  for (const auto& if_name: get_all_if_names()) {
    address_v6 v6 = make_address_v6(std::string("ff00::910:1112:1314%{}" + if_name));
    CHECK(v6.is_multicast());
    CHECK(v6.scope_id() == atoi(if_name.data()));
  }
}

TEST_CASE("[make_address_v6 with ill-formed if-name should set scope id to 0]", "[address_v6]") {
  // empty network interface name
  CHECK(make_address_v6("ff00::910:1112:1314%").is_multicast());
  CHECK(make_address_v6("ff00::910:1112:1314%").scope_id() == 0);

  // invalid network interface name
  CHECK(make_address_v6("ff00::910:1112:1314%||").is_multicast());
  CHECK(make_address_v6("ff00::910:1112:1314%||").scope_id() == 0);
}

TEST_CASE("[get IPv4 address if IPv6 address is v4-mapped]", "[address_v6]") {
  address_v6 v4_mapped_addr{v4_mapped};
  CHECK(v4_mapped_addr.to_string() == "::ffff:0.0.254.255");
  CHECK(v4_mapped_addr.is_v4_mapped());
  CHECK(v4_mapped_addr.to_v4().to_string() == "0.0.254.255");
}

TEST_CASE("[get unspecified IPv4 address if IPv6 address isn't v4-mapped]", "address_v6") {
  address_v6 non_v4_mapped_addr{multicast};
  CHECK(!non_v4_mapped_addr.is_v4_mapped());
  CHECK(non_v4_mapped_addr.to_v4().is_unspecified());
}

TEST_CASE(
  "[pass ill-formed address to make_address_v6 should return unspecified]",
  "[address_v6]") {
  CHECK(make_address_v6("xx:xx").is_unspecified());
  CHECK(make_address_v4("").is_unspecified());
}

TEST_CASE("hash address_v6 should work", "address_v6.hash") {
  std::unordered_map<address_v6, bool> addresses;
  addresses[make_address_v6(multicast)] = false;
  addresses[make_address_v6(multicast_global)] = false;
  CHECK(addresses[make_address_v6(multicast)] == false);
  CHECK(addresses[make_address_v6(multicast_global)] == false);

  addresses[make_address_v6(multicast)] = true;
  CHECK(addresses[make_address_v6(multicast)] == true);
  CHECK(addresses[make_address_v6(multicast_global)] == false);
  addresses[make_address_v6(multicast_global)] = true;
  CHECK(addresses[make_address_v6(multicast_global)] == true);
}

TEST_CASE("[construct address using address_v6]", "[address]") {
  address addr{address_v6{multicast}};
  CHECK(addr.is_multicast());
  CHECK(addr.is_v6());
}

TEST_CASE("[construct address using address_v4]", "[address]") {
  address addr{make_address_v4("127.0.0.1")};
  CHECK(addr.is_loopback());
  CHECK(addr.is_v4());
}

TEST_CASE("[copy constructor of address should work]", "[address]") {
  address addr{address_v6{multicast}};
  address addr_copy{addr};
  CHECK(addr_copy.to_string() == "ff00::910:1112:1314");
  CHECK(addr == addr_copy);
  CHECK(addr.is_v6());
}

TEST_CASE("[move constructor of address should work]", "[address]") {
  address addr{address_v6{std::bit_cast<address_v6::bytes_type>(multicast)}};
  address addr6_copy{std::move(addr)};
  CHECK(addr6_copy.to_string() == "ff00::910:1112:1314");
  CHECK(addr.is_v6());
}

TEST_CASE("[throw exception when try to get wrong version]", "[address.ctor]") {
  address addr_v6{address_v6{std::bit_cast<address_v6::bytes_type>(multicast)}};
  CHECK((addr_v6.is_v6() && !addr_v6.is_v4()));
  CHECK(addr_v6.to_v6().is_multicast());
  REQUIRE_THROWS_AS((addr_v6.to_v4().is_unspecified()), std::bad_variant_access);

  address addr_v4{make_address_v4("127.0.0.1")};
  CHECK((addr_v4.is_v4() && !addr_v4.is_v6()));
  CHECK(addr_v4.to_v4().is_loopback());
  REQUIRE_THROWS_AS((addr_v4.to_v6().is_loopback()), std::bad_variant_access);
}

TEST_CASE("[comparision address]", "[address.comparision]") {
  CHECK(address{make_address_v4("127.0.0.1")} < address{address_v6{loopback}});
  CHECK(address{address_v6{multicast}} == address{address_v6{multicast}});
  CHECK(address{address_v6{v4_mapped}} < address{address_v6{multicast}});
}

TEST_CASE("hash address should work", "address.hash") {
  std::unordered_map<address, bool> addresses;
  addresses[address(make_address_v6(multicast))] = false;
  CHECK(addresses[address(make_address_v6(multicast))] == false);

  addresses[address(make_address_v6(multicast))] = true;
  CHECK(addresses[address(make_address_v6(multicast))] == true);
}
