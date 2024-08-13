/*
 * Copyright (c) 2024 Maikel Nadolski
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
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <bit>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <cstring>
#include <memory>
#include <variant>

namespace sio::ip {
  using scope_id_type = uint_least16_t;

  class address_v4 {
   public:
    using bytes_type = std::array<std::byte, 4>;
    using uint_type = std::uint_least32_t;

    static address_v4 loopback() noexcept {
      return address_v4{INADDR_LOOPBACK};
    }

    static address_v4 broadcast() noexcept {
      return address_v4{INADDR_BROADCAST};
    }

    static address_v4 any() noexcept {
      return address_v4{INADDR_ANY};
    }

    address_v4() = default;

    explicit address_v4(uint_type addr) noexcept
      : addr_{::htonl(addr)} {
    }

    explicit address_v4(bytes_type bytes) noexcept
      : addr_{std::bit_cast<::in_addr_t>(bytes)} {
    }

    uint_type to_uint() const noexcept {
      return ::ntohl(addr_);
    }

    bytes_type to_bytes() const noexcept {
      return std::bit_cast<bytes_type>(addr_);
    }

    template <class _Allocator = std::allocator<char>>
    std::basic_string<char, std::char_traits<char>, _Allocator>
      to_string(_Allocator __alloc = _Allocator()) const {
      char buffer[INET_ADDRSTRLEN + 1] = {};
      inet_ntop(AF_INET, &addr_, buffer, INET_ADDRSTRLEN);
      return std::basic_string<char, std::char_traits<char>, _Allocator>(buffer, __alloc);
    }

    bool is_loopback() const noexcept {
      return (to_uint() & 0xFF000000) == 0x7F000000;
    }

    bool is_multicast() const noexcept {
      return (to_uint() & 0xF0000000) == 0xE0000000;
    }

    bool is_unspecified() const noexcept {
      return to_uint() == 0;
    }

    friend auto operator<=>(const address_v4&, const address_v4&) = default;

   private:
    ::in_addr_t addr_;
  };

  class address_v6 {
   public:
    using bytes_type = std::array<std::byte, 16>;
    using in6_addr_type = ::in6_addr;

    /// Obtain an address object that represents any address.
    static address_v6 any() noexcept {
      return address_v6();
    }

    /// Obtain an address object that represents the loopback address.
    static address_v6 loopback() noexcept {
      address_v6 any{};
      any.addr_.s6_addr[15] = 1;
      return any;
    }

    address_v6() noexcept
      : scope_id_(0) {
      ::memset(addr_.s6_addr, 0, sizeof(addr_.s6_addr));
    }

    explicit address_v6(const bytes_type& bytes, scope_id_type scope_id = 0)
      : addr_(std::bit_cast<in6_addr_type>(bytes))
      , scope_id_(scope_id) {
    }

    scope_id_type scope_id() const noexcept {
      return scope_id_;
    }

    void scope_id(scope_id_type id) noexcept {
      scope_id_ = id;
    }

    bytes_type to_bytes() const noexcept {
      return std::bit_cast<bytes_type>(addr_);
    }

    address_v4 to_v4() const noexcept {
      if (!is_v4_mapped()) {
        return {};
      }
      auto bytes = to_bytes();
      return address_v4(address_v4::bytes_type{bytes[12], bytes[13], bytes[14], bytes[15]});
    }

    template <class _Allocator = std::allocator<char>>
    std::basic_string<char, std::char_traits<char>, _Allocator>
      to_string(_Allocator __alloc = _Allocator()) const {
      char buffer[INET6_ADDRSTRLEN + 1] = {};
      ::inet_ntop(AF_INET6, &addr_, buffer, INET6_ADDRSTRLEN);
      return std::string(buffer, __alloc);
    }

    bool is_loopback() const noexcept {
      return (addr_.s6_addr[0] == 0) && (addr_.s6_addr[1] == 0) && (addr_.s6_addr[2] == 0)
          && (addr_.s6_addr[3] == 0) && (addr_.s6_addr[4] == 0) && (addr_.s6_addr[5] == 0)
          && (addr_.s6_addr[6] == 0) && (addr_.s6_addr[7] == 0) && (addr_.s6_addr[8] == 0)
          && (addr_.s6_addr[9] == 0) && (addr_.s6_addr[10] == 0) && (addr_.s6_addr[11] == 0)
          && (addr_.s6_addr[12] == 0) && (addr_.s6_addr[13] == 0) && (addr_.s6_addr[14] == 0)
          && (addr_.s6_addr[15] == 1);
    }

    bool is_unspecified() const noexcept {
      return (addr_.s6_addr[0] == 0) && (addr_.s6_addr[1] == 0) && (addr_.s6_addr[2] == 0)
          && (addr_.s6_addr[3] == 0) && (addr_.s6_addr[4] == 0) && (addr_.s6_addr[5] == 0)
          && (addr_.s6_addr[6] == 0) && (addr_.s6_addr[7] == 0) && (addr_.s6_addr[8] == 0)
          && (addr_.s6_addr[9] == 0) && (addr_.s6_addr[10] == 0) && (addr_.s6_addr[11] == 0)
          && (addr_.s6_addr[12] == 0) && (addr_.s6_addr[13] == 0) && (addr_.s6_addr[14] == 0)
          && (addr_.s6_addr[15] == 0);
    }

    bool is_link_local() const noexcept {
      return (addr_.s6_addr[0] == 0xfe) && ((addr_.s6_addr[1] & 0xc0) == 0x80);
    }

    bool is_site_local() const noexcept {
      return (addr_.s6_addr[0] == 0xfe) && ((addr_.s6_addr[1] & 0xc0) == 0xc0);
    }

    bool is_v4_mapped() const noexcept {
      return (addr_.s6_addr[0] == 0) && (addr_.s6_addr[1] == 0) && (addr_.s6_addr[2] == 0)
          && (addr_.s6_addr[3] == 0) && (addr_.s6_addr[4] == 0) && (addr_.s6_addr[5] == 0)
          && (addr_.s6_addr[6] == 0) && (addr_.s6_addr[7] == 0) && (addr_.s6_addr[8] == 0)
          && (addr_.s6_addr[9] == 0) && (addr_.s6_addr[10] == 0xff) && (addr_.s6_addr[11] == 0xff);
    }

    bool is_multicast() const noexcept {
      return addr_.s6_addr[0] == 0xff;
    }

    bool is_multicast_global() const noexcept {
      return (addr_.s6_addr[0] == 0xff) && ((addr_.s6_addr[1] & 0x0f) == 0x0e);
    }

    bool is_multicast_link_local() const noexcept {
      return (addr_.s6_addr[0] == 0xff) && ((addr_.s6_addr[1] & 0x0f) == 0x02);
    }

    bool is_multicast_node_local() const noexcept {
      return (addr_.s6_addr[0] == 0xff) && ((addr_.s6_addr[1] & 0x0f) == 0x01);
    }

    bool is_multicast_org_local() const noexcept {
      return (addr_.s6_addr[0] == 0xff) && ((addr_.s6_addr[1] & 0x0f) == 0x08);
    }

    bool is_multicast_site_local() const noexcept {
      return (addr_.s6_addr[0] == 0xff) && ((addr_.s6_addr[1] & 0x0f) == 0x05);
    }

    /// Compare two addresses for equality.
    friend bool operator==(const address_v6& a1, const address_v6& a2) noexcept {
      return ::memcmp(&a1.addr_, &a2.addr_, sizeof(in6_addr_type)) == 0
          && a1.scope_id_ == a2.scope_id_;
    }

    /// Compare two addresses for inequality.
    friend bool operator!=(const address_v6& a1, const address_v6& a2) noexcept {
      return !(a1 == a2);
    }

    /// Compare addresses for ordering.
    friend bool operator<(const address_v6& a1, const address_v6& a2) noexcept {
      int result = ::memcmp(&a1.addr_, &a2.addr_, sizeof(in6_addr_type));
      if (result < 0) {
        return true;
      }
      if (result > 0) {
        return false;
      }
      return a1.scope_id_ < a2.scope_id_;
    }

    /// Compare addresses for ordering.
    friend bool operator>(const address_v6& a1, const address_v6& a2) noexcept {
      return a2 < a1;
    }

    /// Compare addresses for ordering.
    friend bool operator<=(const address_v6& a1, const address_v6& a2) noexcept {
      return !(a2 < a1);
    }

    /// Compare addresses for ordering.
    friend bool operator>=(const address_v6& a1, const address_v6& a2) noexcept {
      return !(a1 < a2);
    }

   private:
    // The underlying IPv6 address.
    in6_addr_type addr_;

    // The scope ID associated with the address.
    scope_id_type scope_id_;
  };

  class address {
   public:
    address() = default;

    address(address_v4 other) noexcept
      : addr_(other) {
    }

    address(address_v6 other) noexcept
      : addr_(other) {
    }

    bool is_v4() const noexcept {
      return std::holds_alternative<address_v4>(addr_);
    }

    bool is_v6() const noexcept {
      return std::holds_alternative<address_v6>(addr_);
    }

    /// Get the address as an IP version 4 address.
    address_v4 to_v4() const {
      return std::get<address_v4>(addr_);
    }

    /// Get the address as an IP version 6 address.
    address_v6 to_v6() const {
      return std::get<address_v6>(addr_);
    }

    /// Get the address as a string.
    std::string to_string() const {
      if (is_v4()) {
        return to_v4().to_string();
      } else {
        return to_v6().to_string();
      }
    }

    /// Determine whether the address is a loopback address.
    bool is_loopback() const noexcept {
      if (is_v4()) {
        return to_v4().is_loopback();
      } else {
        return to_v6().is_loopback();
      }
    }

    /// Determine whether the address is unspecified.
    bool is_unspecified() const noexcept {
      if (is_v4()) {
        return to_v4().is_unspecified();
      } else {
        return to_v6().is_unspecified();
      }
    }

    /// Determine whether the address is a multicast address.
    bool is_multicast() const noexcept {
      if (is_v4()) {
        return to_v4().is_multicast();
      } else {
        return to_v6().is_multicast();
      }
    }

    /// Compare two addresses for equality.
    friend bool operator==(const address&, const address&) = default;

    /// Compare two addresses for inequality.
    friend bool operator!=(const address& a1, const address& a2) noexcept {
      return !(a1 == a2);
    }

    /// Compare addresses for ordering.
    friend bool operator<(const address& a1, const address& a2) noexcept {
      if (a1.addr_.index() != a2.addr_.index()) {
        return a1.addr_.index() < a2.addr_.index();
      }

      return a1.addr_.index() == 0
             ? std::get<0>(a1.addr_) < std::get<0>(a2.addr_)
             : std::get<1>(a1.addr_) < std::get<1>(a2.addr_);
    }

    /// Compare addresses for ordering.
    friend bool operator>(const address& a1, const address& a2) noexcept {
      return a2 < a1;
    }

    /// Compare addresses for ordering.
    friend bool operator<=(const address& a1, const address& a2) noexcept {
      return !(a2 < a1);
    }

    /// Compare addresses for ordering.
    friend bool operator>=(const address& a1, const address& a2) noexcept {
      return !(a1 < a2);
    }


   private:
    std::variant<address_v4, address_v6> addr_{};
  };

  // Create an IPv4 address from an unsigned integer in host byte order.
  inline address_v4 make_address_v4(address_v4::uint_type addr) noexcept {
    return address_v4(addr);
  }

  // Create an IPv4 address from an IP address string in dotted decimal form.
  inline address_v4 make_address_v4(const char* addr) noexcept {
    address_v4::bytes_type bytes;
    return ::inet_pton(AF_INET, addr, &bytes[0]) > 0 ? address_v4(bytes) : address_v4{};
  }

  // Create an IPv4 address from an IP address string in dotted decimal form.
  inline address_v4 make_address_v4(const std::string& addr) noexcept {
    return make_address_v4(addr.c_str());
  }

  // Create an IPv4 address from an IP address string in dotted decimal form.
  inline address_v4 make_address_v4(std::string_view addr) noexcept {
    return make_address_v4(static_cast<std::string>(addr));
  }

  // Create an IPv6 address from raw bytes and scope ID.
  inline address_v6
    make_address_v6(const address_v6::bytes_type& bytes, uint32_t scope_id = 0) noexcept {
    return address_v6(bytes, scope_id);
  }

  // Create an IPv6 address from an IP address string.
  inline address_v6 make_address_v6(const char* str) noexcept {
    const char* if_name = ::strchr(str, '%');
    const char* p = str;
    char addr_buf[INET6_ADDRSTRLEN + 1]{};

    if (if_name) {
      if (if_name - str > INET6_ADDRSTRLEN) {
        return address_v6{};
      }
      ::memcpy(addr_buf, str, if_name - str);
      addr_buf[if_name - str] = '\0';
      p = addr_buf;
    }

    ::in6_addr addr;
    if (::inet_pton(AF_INET6, p, &addr) > 0) {
      scope_id_type scope_id = 0;

      // Get scope id by network interface name.
      if (if_name) {
        bool is_link_local = addr.s6_addr[0] == 0xfe && ((addr.s6_addr[1] & 0xc0) == 0x80);
        bool is_multicast_link_local = addr.s6_addr[0] == 0xff && addr.s6_addr[1] == 0x02;
        if (is_link_local || is_multicast_link_local) {
          scope_id = ::if_nametoindex(if_name + 1);
        }
        if (scope_id == 0) {
          scope_id = ::atoi(if_name + 1);
        }
      }
      return address_v6{std::bit_cast<address_v6::bytes_type>(addr), scope_id};
    }
    return address_v6{};
  }

  // Create IPv6 address from an IP address string.
  inline address_v6 make_address_v6(const std::string& str) noexcept {
    return make_address_v6(str.c_str());
  }

  // Create an IPv6 address from an IP address string.
  inline address_v6 make_address_v6(std::string_view str) noexcept {
    return make_address_v6(static_cast<std::string>(str));
  }

  // Tag type used for distinguishing overloads that deal in IPv4-mapped IPv6
  // addresses.
  enum class v4_mapped_t {
    v4_mapped
  };

  // Create an IPv4 address from a IPv4-mapped IPv6 address.
  inline address_v4 make_address_v4(v4_mapped_t, const address_v6& v6_addr) {
    if (!v6_addr.is_v4_mapped()) {
      return {};
    }
    address_v6::bytes_type v6_bytes = v6_addr.to_bytes();
    return address_v4{
      address_v4::bytes_type{v6_bytes[12], v6_bytes[13], v6_bytes[14], v6_bytes[15]}
    };
  }

  // Create an IPv4-mapped IPv6 address from an IPv4 address.
  inline address_v6 make_address_v6(v4_mapped_t, const address_v4& v4_addr) {
    address_v4::bytes_type v4_bytes = v4_addr.to_bytes();
    address_v6::bytes_type v6_bytes{
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0xFF},
      std::byte{0xFF},
      v4_bytes[0],
      v4_bytes[1],
      v4_bytes[2],
      v4_bytes[3]};
    return address_v6{v6_bytes};
  }


}

namespace std {
  template <>
  struct hash<sio::ip::address_v4> {
    std::size_t operator()(const sio::ip::address_v4& addr) const noexcept {
      return std::hash<unsigned int>()(addr.to_uint());
    };
  };

  template <>
  struct hash<sio::ip::address_v6> {
    std::size_t operator()(const sio::ip::address_v6& addr) const noexcept {
      const sio::ip::address_v6::bytes_type bytes = addr.to_bytes();
      auto result = static_cast<std::size_t>(addr.scope_id());
      combine_4_bytes(&result, &bytes[0]);
      combine_4_bytes(&result, &bytes[4]);
      combine_4_bytes(&result, &bytes[8]);
      combine_4_bytes(&result, &bytes[12]);
      return result;
    }

   private:
    static void combine_4_bytes(std::size_t* seed, const std::byte* bytes) {
      std::byte bytes_hash = bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
      *seed ^= std::to_integer<size_t>(bytes_hash) + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);
    }
  };

  template <>
  struct hash<sio::ip::address> {
    std::size_t operator()(const sio::ip::address& addr) const noexcept {
      return addr.is_v4() ? std::hash<sio::ip::address_v4>()(addr.to_v4())
                          : std::hash<sio::ip::address_v6>()(addr.to_v6());
    }
  };

}
