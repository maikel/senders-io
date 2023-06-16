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

namespace sio::net::ip {
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
      char __buffer[INET_ADDRSTRLEN + 1] = {};
      inet_ntop(AF_INET, &addr_, __buffer, INET_ADDRSTRLEN);
      return std::basic_string<char, std::char_traits<char>, _Allocator>(__buffer, __alloc);
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
      address_v6 __any{};
      __any.__addr_.s6_addr[15] = 1;
      return __any;
    }

    address_v6() noexcept
      : __scope_id_(0) {
      ::memset(__addr_.s6_addr, 0, sizeof(__addr_.s6_addr));
    }

    explicit address_v6(const bytes_type& __bytes, scope_id_type __scope_id = 0)
      : __addr_(std::bit_cast<in6_addr_type>(__bytes))
      , __scope_id_(__scope_id) {
    }

    scope_id_type scope_id() const noexcept {
      return __scope_id_;
    }

    void scope_id(scope_id_type id) noexcept {
      __scope_id_ = id;
    }

    bytes_type to_bytes() const noexcept {
      return std::bit_cast<bytes_type>(__addr_);
    }

    address_v4 to_v4() const noexcept {
      if (!is_v4_mapped()) {
        return {};
      }
      auto __bytes = to_bytes();
      return address_v4(address_v4::bytes_type{__bytes[12], __bytes[13], __bytes[14], __bytes[15]});
    }

    template <class _Allocator = std::allocator<char>>
    std::basic_string<char, std::char_traits<char>, _Allocator>
      to_string(_Allocator __alloc = _Allocator()) const {
      char __buffer[INET6_ADDRSTRLEN + 1] = {};
      ::inet_ntop(AF_INET6, &__addr_, __buffer, INET6_ADDRSTRLEN);
      return std::string(__buffer, __alloc);
    }

    bool is_loopback() const noexcept {
      return (__addr_.s6_addr[0] == 0) && (__addr_.s6_addr[1] == 0) && (__addr_.s6_addr[2] == 0)
          && (__addr_.s6_addr[3] == 0) && (__addr_.s6_addr[4] == 0) && (__addr_.s6_addr[5] == 0)
          && (__addr_.s6_addr[6] == 0) && (__addr_.s6_addr[7] == 0) && (__addr_.s6_addr[8] == 0)
          && (__addr_.s6_addr[9] == 0) && (__addr_.s6_addr[10] == 0) && (__addr_.s6_addr[11] == 0)
          && (__addr_.s6_addr[12] == 0) && (__addr_.s6_addr[13] == 0) && (__addr_.s6_addr[14] == 0)
          && (__addr_.s6_addr[15] == 1);
    }

    bool is_unspecified() const noexcept {
      return (__addr_.s6_addr[0] == 0) && (__addr_.s6_addr[1] == 0) && (__addr_.s6_addr[2] == 0)
          && (__addr_.s6_addr[3] == 0) && (__addr_.s6_addr[4] == 0) && (__addr_.s6_addr[5] == 0)
          && (__addr_.s6_addr[6] == 0) && (__addr_.s6_addr[7] == 0) && (__addr_.s6_addr[8] == 0)
          && (__addr_.s6_addr[9] == 0) && (__addr_.s6_addr[10] == 0) && (__addr_.s6_addr[11] == 0)
          && (__addr_.s6_addr[12] == 0) && (__addr_.s6_addr[13] == 0) && (__addr_.s6_addr[14] == 0)
          && (__addr_.s6_addr[15] == 0);
    }

    bool is_link_local() const noexcept {
      return (__addr_.s6_addr[0] == 0xfe) && ((__addr_.s6_addr[1] & 0xc0) == 0x80);
    }

    bool is_site_local() const noexcept {
      return (__addr_.s6_addr[0] == 0xfe) && ((__addr_.s6_addr[1] & 0xc0) == 0xc0);
    }

    bool is_v4_mapped() const noexcept {
      return (__addr_.s6_addr[0] == 0) && (__addr_.s6_addr[1] == 0) && (__addr_.s6_addr[2] == 0)
          && (__addr_.s6_addr[3] == 0) && (__addr_.s6_addr[4] == 0) && (__addr_.s6_addr[5] == 0)
          && (__addr_.s6_addr[6] == 0) && (__addr_.s6_addr[7] == 0) && (__addr_.s6_addr[8] == 0)
          && (__addr_.s6_addr[9] == 0) && (__addr_.s6_addr[10] == 0xff)
          && (__addr_.s6_addr[11] == 0xff);
    }

    bool is_multicast() const noexcept {
      return __addr_.s6_addr[0] == 0xff;
    }

    bool is_multicast_global() const noexcept {
      return (__addr_.s6_addr[0] == 0xff) && ((__addr_.s6_addr[1] & 0x0f) == 0x0e);
    }

    bool is_multicast_link_local() const noexcept {
      return (__addr_.s6_addr[0] == 0xff) && ((__addr_.s6_addr[1] & 0x0f) == 0x02);
    }

    bool is_multicast_node_local() const noexcept {
      return (__addr_.s6_addr[0] == 0xff) && ((__addr_.s6_addr[1] & 0x0f) == 0x01);
    }

    bool is_multicast_org_local() const noexcept {
      return (__addr_.s6_addr[0] == 0xff) && ((__addr_.s6_addr[1] & 0x0f) == 0x08);
    }

    bool is_multicast_site_local() const noexcept {
      return (__addr_.s6_addr[0] == 0xff) && ((__addr_.s6_addr[1] & 0x0f) == 0x05);
    }

    /// Compare two addresses for equality.
    friend bool operator==(const address_v6& a1, const address_v6& a2) noexcept {
      return ::memcmp(&a1.__addr_, &a2.__addr_, sizeof(in6_addr_type)) == 0
          && a1.__scope_id_ == a2.__scope_id_;
    }

    /// Compare two addresses for inequality.
    friend bool operator!=(const address_v6& a1, const address_v6& a2) noexcept {
      return !(a1 == a2);
    }

    /// Compare addresses for ordering.
    friend bool operator<(const address_v6& a1, const address_v6& a2) noexcept {
      int __result = ::memcmp(&a1.__addr_, &a2.__addr_, sizeof(in6_addr_type));
      if (__result < 0) {
        return true;
      }
      if (__result > 0) {
        return false;
      }
      return a1.__scope_id_ < a2.__scope_id_;
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
    in6_addr_type __addr_;

    // The scope ID associated with the address.
    scope_id_type __scope_id_;
  };

  class address {
   public:
    address() = default;

    address(address_v4 __other) noexcept
      : __addr_(__other) {
    }

    address(address_v6 __other) noexcept
      : __addr_(__other) {
    }

    bool is_v4() const noexcept {
      return std::holds_alternative<address_v4>(__addr_);
    }

    bool is_v6() const noexcept {
      return std::holds_alternative<address_v6>(__addr_);
    }

    /// Get the address as an IP version 4 address.
    address_v4 to_v4() const {
      return std::get<address_v4>(__addr_);
    }

    /// Get the address as an IP version 6 address.
    address_v6 to_v6() const {
      return std::get<address_v6>(__addr_);
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
      if (a1.__addr_.index() != a2.__addr_.index()) {
        return a1.__addr_.index() < a2.__addr_.index();
      }

      return a1.__addr_.index() == 0
             ? std::get<0>(a1.__addr_) < std::get<0>(a2.__addr_)
             : std::get<1>(a1.__addr_) < std::get<1>(a2.__addr_);
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
    std::variant<address_v4, address_v6> __addr_{};
  };

  // Create an IPv4 address from an unsigned integer in host byte order.
  inline address_v4 make_address_v4(address_v4::uint_type __addr) noexcept {
    return address_v4(__addr);
  }

  // Create an IPv4 address from an IP address string in dotted decimal form.
  inline address_v4 make_address_v4(const char* __addr) noexcept {
    address_v4::bytes_type __bytes;
    return ::inet_pton(AF_INET, __addr, &__bytes[0]) > 0 ? address_v4(__bytes) : address_v4{};
  }

  // Create an IPv4 address from an IP address string in dotted decimal form.
  inline address_v4 make_address_v4(const std::string& __addr) noexcept {
    return make_address_v4(__addr.c_str());
  }

  // Create an IPv4 address from an IP address string in dotted decimal form.
  inline address_v4 make_address_v4(std::string_view __addr) noexcept {
    return make_address_v4(static_cast<std::string>(__addr));
  }

  // Create an IPv6 address from raw bytes and scope ID.
  inline address_v6
    make_address_v6(const address_v6::bytes_type& __bytes, uint32_t __scope_id = 0) noexcept {
    return address_v6(__bytes, __scope_id);
  }

  // Create an IPv6 address from an IP address string.
  inline address_v6 make_address_v6(const char* __str) noexcept {
    const char* __if_name = ::strchr(__str, '%');
    const char* __p = __str;
    char __addr_buf[INET6_ADDRSTRLEN + 1]{};

    if (__if_name) {
      if (__if_name - __str > INET6_ADDRSTRLEN) {
        return address_v6{};
      }
      ::memcpy(__addr_buf, __str, __if_name - __str);
      __addr_buf[__if_name - __str] = '\0';
      __p = __addr_buf;
    }

    ::in6_addr __addr;
    if (::inet_pton(AF_INET6, __p, &__addr) > 0) {
      scope_id_type __scope_id = 0;

      // Get scope id by network interface name.
      if (__if_name) {
        bool __is_link_local = __addr.s6_addr[0] == 0xfe && ((__addr.s6_addr[1] & 0xc0) == 0x80);
        bool __is_multicast_link_local = __addr.s6_addr[0] == 0xff && __addr.s6_addr[1] == 0x02;
        if (__is_link_local || __is_multicast_link_local) {
          __scope_id = ::if_nametoindex(__if_name + 1);
        }
        if (__scope_id == 0) {
          __scope_id = ::atoi(__if_name + 1);
        }
      }
      return address_v6{std::bit_cast<address_v6::bytes_type>(__addr), __scope_id};
    }
    return address_v6{};
  }

  // Create IPv6 address from an IP address string.
  inline address_v6 make_address_v6(const std::string& __str) noexcept {
    return make_address_v6(__str.c_str());
  }

  // Create an IPv6 address from an IP address string.
  inline address_v6 make_address_v6(std::string_view __str) noexcept {
    return make_address_v6(static_cast<std::string>(__str));
  }

  // Tag type used for distinguishing overloads that deal in IPv4-mapped IPv6
  // addresses.
  enum class v4_mapped_t {
    v4_mapped
  };

  // Create an IPv4 address from a IPv4-mapped IPv6 address.
  inline address_v4 make_address_v4(v4_mapped_t, const address_v6& __v6_addr) {
    if (!__v6_addr.is_v4_mapped()) {
      return {};
    }
    address_v6::bytes_type __v6_bytes = __v6_addr.to_bytes();
    return address_v4{
      address_v4::bytes_type{__v6_bytes[12], __v6_bytes[13], __v6_bytes[14], __v6_bytes[15]}
    };
  }

  // Create an IPv4-mapped IPv6 address from an IPv4 address.
  inline address_v6 make_address_v6(v4_mapped_t, const address_v4& v4_addr) {
    address_v4::bytes_type __v4_bytes = v4_addr.to_bytes();
    address_v6::bytes_type __v6_bytes{
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
      __v4_bytes[0],
      __v4_bytes[1],
      __v4_bytes[2],
      __v4_bytes[3]};
    return address_v6{__v6_bytes};
  }


}

namespace std {
  template <>
  struct hash<sio::net::ip::address_v4> {
    std::size_t operator()(const sio::net::ip::address_v4& __addr) const noexcept {
      return std::hash<unsigned int>()(__addr.to_uint());
    };
  };

  template <>
  struct hash<sio::net::ip::address_v6> {
    std::size_t operator()(const sio::net::ip::address_v6& addr) const noexcept {
      const sio::net::ip::address_v6::bytes_type __bytes = addr.to_bytes();
      auto __result = static_cast<std::size_t>(addr.scope_id());
      __combine_4_bytes(&__result, &__bytes[0]);
      __combine_4_bytes(&__result, &__bytes[4]);
      __combine_4_bytes(&__result, &__bytes[8]);
      __combine_4_bytes(&__result, &__bytes[12]);
      return __result;
    }

   private:
    static void __combine_4_bytes(std::size_t* __seed, const std::byte* bytes) {
      std::byte __bytes_hash = bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
      *__seed ^= std::to_integer<size_t>(__bytes_hash) + 0x9e3779b9 + (*__seed << 6)
               + (*__seed >> 2);
    }
  };

  template <>
  struct hash<sio::net::ip::address> {
    std::size_t operator()(const sio::net::ip::address& addr) const noexcept {
      return addr.is_v4() ? std::hash<sio::net::ip::address_v4>()(addr.to_v4())
                          : std::hash<sio::net::ip::address_v6>()(addr.to_v6());
    }
  };

}
