/*
 * Copyright (c) 2024 Maikel Nadolski
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

#include <sys/socket.h>
#include <linux/can.h>

#include <cstddef>
#include <cstring>

namespace sio::can {

  class endpoint {
   public:
    using native_handle_type = ::sockaddr_can;

    explicit endpoint(int ifindex) noexcept
      : addr_{.can_family = PF_CAN, .can_ifindex = ifindex} {
    }

    const ::sockaddr_can* data() const noexcept {
      return &addr_;
    }

    std::size_t size() const noexcept {
      return sizeof(addr_);
    }

    friend bool operator==(const endpoint& e1, const endpoint& e2) noexcept {
      return !std::memcmp(e1.data(), e2.data(), e1.size());
    }

    friend bool operator!=(const endpoint& e1, const endpoint& e2) noexcept {
      return !(e1 == e2);
    }

   private:
    ::sockaddr_can addr_{.can_family = PF_CAN};
  };

}