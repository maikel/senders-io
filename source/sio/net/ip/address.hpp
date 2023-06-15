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

#include <array>
#include <bit>
#include <cstdint>
#include <string>

#include <variant>

#include <arpa/inet.h>
#include <netinet/in.h>

namespace sio::net::ip {

  using in6_addr_type = ::in6_addr;

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
      return *this == loopback();
    }

    bool is_multicast() const noexcept;

    bool is_unspecified() const noexcept;

    friend auto operator<=>(const address_v4&, const address_v4&) = default;

   private:
    ::in_addr_t addr_;
  };

  class address_v6 {
   public:
    using bytes_type = std::array<std::byte, 16>;

    /// Obtain an address object that represents any address.
    static address_v6 any() noexcept {
      return address_v6();
    }

    /// Obtain an address object that represents the loopback address.
    static address_v6 loopback() noexcept;

    address_v6() noexcept;

    explicit address_v6(const bytes_type& bytes, unsigned long __scope_id = 0) {
    }

    unsigned long scope_id() const noexcept {
      return scope_id_;
    }

    void scope_id(unsigned long id) noexcept {
      scope_id_ = id;
    }

    bytes_type to_bytes() const noexcept {
      return std::bit_cast<bytes_type>(addr_);
    }

    template <class _Allocator = std::allocator<char>>
    std::string to_string(_Allocator alloc = _Allocator()) const {
      char buffer[INET6_ADDRSTRLEN + 1] = {};
      inet_ntop(AF_INET6, &addr_, buffer, INET6_ADDRSTRLEN);
      return std::basic_string<char, std::char_traits<char>, _Allocator>(buffer);
    }

    bool is_loopback() const noexcept;
    bool is_unspecified() const noexcept;
    bool is_link_local() const noexcept;
    bool is_site_local() const noexcept;
    bool is_v4_mapped() const noexcept;
    bool is_multicast() const noexcept;
    bool is_multicast_global() const noexcept;
    bool is_multicast_link_local() const noexcept;
    bool is_multicast_node_local() const noexcept;
    bool is_multicast_org_local() const noexcept;
    bool is_multicast_site_local() const noexcept;

    /// Compare two addresses for equality.
    friend bool operator==(const address_v6& a1, const address_v6& a2) noexcept;

    /// Compare two addresses for inequality.
    friend bool operator!=(const address_v6& a1, const address_v6& a2) noexcept {
      return !(a1 == a2);
    }

    /// Compare addresses for ordering.
    friend bool operator<(const address_v6& a1, const address_v6& a2) noexcept;

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
    unsigned long scope_id_;
  };

  class address {
   public:
    address() = default;

    address(address_v4 __other) noexcept
      : addr_(__other) {
    }

    address(address_v6 __other) noexcept
      : addr_(__other) {
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

    friend bool operator==(const address&, const address&) = default;

   private:
    std::variant<address_v4, address_v6> addr_{};
  };

  class endpoint {
   public:
    endpoint() = default;

    explicit endpoint(int __family, unsigned short __port) noexcept {
      if (__family == AF_INET) {
        __data_.__v4.sin_family = AF_INET;
        __data_.__v4.sin_port = ::htons(__port);
        __data_.__v4.sin_addr.s_addr = INADDR_ANY;
      } else if (__family == AF_INET6) {
        __data_.__v6.sin6_family = AF_INET6;
        __data_.__v6.sin6_port = ::htons(__port);
        __data_.__v6.sin6_addr = IN6ADDR_ANY_INIT;
      }
    }

    explicit endpoint(net::ip::address addr, unsigned short __port) noexcept {
      if (addr.is_v4()) {
        __data_.__v4.sin_family = AF_INET;
        __data_.__v4.sin_port = ::htons(__port);
        __data_.__v4.sin_addr = bit_cast<::in_addr>(addr.to_v4().to_bytes());
      } else {
        __data_.__v6.sin6_family = AF_INET6;
        __data_.__v6.sin6_port = ::htons(__port);
        __data_.__v6.sin6_addr = bit_cast<::in6_addr>(addr.to_v6().to_bytes());
      }
    }

    bool is_v4() const noexcept {
      return __data_.__base.sa_family == AF_INET;
    }

    unsigned short port() const noexcept {
      if (is_v4()) {
        return ::ntohs(__data_.__v4.sin_port);
      } else {
        return ::ntohs(__data_.__v6.sin6_port);
      }
    }

    ::sio::net::ip::address address() const noexcept {
      if (is_v4()) {
        return address_v4{std::bit_cast<address_v4::bytes_type>(__data_.__v4.sin_addr)};
      } else {
        return address_v6{std::bit_cast<address_v6::bytes_type>(__data_.__v6.sin6_addr)};
      }
    }

   private:
    union {
      ::sockaddr __base;
      ::sockaddr_in __v4;
      ::sockaddr_in6 __v6;
    } __data_{};
  };
}