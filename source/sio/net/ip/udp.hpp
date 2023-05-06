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

#include <netinet/in.h>

namespace exec::net::ip {

  class udp {
   public:
    static udp v4() {
      return udp(AF_INET);
    }

    static udp v6() {
      return udp(AF_INET6);
    }

    int type() const {
      return SOCK_DGRAM;
    }

    int protocol() const {
      return IPPROTO_UDP;
    }

    int family() const {
      return family_;
    }

    friend bool operator==(const udp& p1, const udp& p2) {
      return p1.family_ == p2.family_;
    }

    friend bool operator!=(const udp& p1, const udp& p2) {
      return p1.family_ != p2.family_;
    }

   private:
    explicit udp(int protocol_family)
      : family_(protocol_family) {
    }

    int family_;
  };

}
