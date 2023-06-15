/*
 * Copyright (c) 2023 Xiaoming Zhang
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

#include <netinet/in.h>
#include <bit>

#include "address.hpp"

namespace sio::net::ip {
  using port_type = uint_least16_t;

  class endpoint {
   public:
    endpoint() = default;

    explicit endpoint(int __family, port_type __port) noexcept {
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

    explicit endpoint(net::ip::address __addr, port_type __port) noexcept {
      if (__addr.is_v4()) {
        __data_.__v4.sin_family = AF_INET;
        __data_.__v4.sin_port = ::htons(__port);
        __data_.__v4.sin_addr = bit_cast<::in_addr>(__addr.to_v4().to_bytes());
      } else {
        __data_.__v6.sin6_family = AF_INET6;
        __data_.__v6.sin6_port = ::htons(__port);
        __data_.__v6.sin6_addr = bit_cast<::in6_addr>(__addr.to_v6().to_bytes());
      }
    }

    bool is_v4() const noexcept {
      return __data_.__base.sa_family == AF_INET;
    }

    port_type port() const noexcept {
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

    friend bool operator==(const endpoint& e1, const endpoint& e2) noexcept {
      return e1.address() == e2.address() && e1.port() == e2.port();
    }

    friend bool operator!=(const endpoint& e1, const endpoint& e2) noexcept {
      return !(e1 == e2);
    }

    friend bool operator<(const endpoint& e1, const endpoint& e2) noexcept {
      if (e1.address() < e2.address()) {
        return true;
      }
      if (e1.address() != e2.address()) {
        return false;
      }
      return e1.port() < e2.port();
    }

    friend bool operator>(const endpoint& e1, const endpoint& e2) noexcept {
      return e2 < e1;
    }

    friend bool operator<=(const endpoint& e1, const endpoint& e2) noexcept {
      return !(e2 < e1);
    }

    friend bool operator>=(const endpoint& e1, const endpoint& e2) noexcept {
      return !(e1 < e2);
    }


   private:
    union {
      ::sockaddr __base;
      ::sockaddr_in __v4;
      ::sockaddr_in6 __v6;
    } __data_{};
  };
}

namespace std {
  template <>
  struct hash<sio::net::ip::endpoint> {
    std::size_t operator()(const sio::net::ip::endpoint& __ep) const noexcept {
      std::size_t __hash1 = std::hash<sio::net::ip::address>()(__ep.address());
      std::size_t __hash2 = std::hash<sio::net::ip::port_type>()(__ep.port());
      return __hash1 ^ (__hash2 + 0x9e3779b9 + (__hash1 << 6) + (__hash1 >> 2));
    }
  };

}
