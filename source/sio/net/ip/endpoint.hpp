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

namespace sio::ip {
  using port_type = uint_least16_t;

  class endpoint {
   public:
    endpoint() = default;

    explicit endpoint(int family, port_type port) noexcept {
      if (family == AF_INET) {
        data_.v4.sin_family = AF_INET;
        data_.v4.sin_port = ::htons(port);
        data_.v4.sin_addr.s_addr = INADDR_ANY;
      } else if (family == AF_INET6) {
        data_.v6.sin6_family = AF_INET6;
        data_.v6.sin6_port = ::htons(port);
        data_.v6.sin6_addr = IN6ADDR_ANY_INIT;
      }
    }

    explicit endpoint(ip::address addr, port_type port) noexcept {
      if (addr.is_v4()) {
        data_.v4.sin_family = AF_INET;
        data_.v4.sin_port = ::htons(port);
        data_.v4.sin_addr = bit_cast<::in_addr>(addr.to_v4().to_bytes());
      } else {
        data_.v6.sin6_family = AF_INET6;
        data_.v6.sin6_port = ::htons(port);
        data_.v6.sin6_addr = bit_cast<::in6_addr>(addr.to_v6().to_bytes());
      }
    }

    bool is_v4() const noexcept {
      return data_.base.sa_family == AF_INET;
    }

    port_type port() const noexcept {
      if (is_v4()) {
        return ::ntohs(data_.v4.sin_port);
      } else {
        return ::ntohs(data_.v6.sin6_port);
      }
    }

    ::sio::ip::address address() const noexcept {
      if (is_v4()) {
        return address_v4{std::bit_cast<address_v4::bytes_type>(data_.v4.sin_addr)};
      } else {
        return address_v6{std::bit_cast<address_v6::bytes_type>(data_.v6.sin6_addr)};
      }
    }

    ::sockaddr* data() noexcept {
      return &data_.base;
    }

    const ::sockaddr* data() const noexcept {
      return &data_.base;
    }

    ::socklen_t size() const noexcept {
      if (is_v4()) {
        return sizeof(data_.v4);
      } else {
        return sizeof(data_.v6);
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
      ::sockaddr base;
      ::sockaddr_in v4;
      ::sockaddr_in6 v6;
    } data_{};
  };
}

namespace std {
  template <>
  struct hash<sio::ip::endpoint> {
    std::size_t operator()(const sio::ip::endpoint& ep) const noexcept {
      std::size_t hash1 = std::hash<sio::ip::address>()(ep.address());
      std::size_t hash2 = std::hash<sio::ip::port_type>()(ep.port());
      return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
    }
  };

}
