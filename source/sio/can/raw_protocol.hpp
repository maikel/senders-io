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

#include "./endpoint.hpp"

#include <sys/socket.h>
#include <linux/can.h>

namespace sio::can {

  class raw_protocol {
   public:
    using endpoint = can::endpoint;

    int type() const {
      return SOCK_RAW;
    }

    int protocol() const {
      return CAN_RAW;
    }

    int family() const {
      return PF_CAN;
    }

    friend bool operator==(const raw_protocol& p1, const raw_protocol& p2) {
      return true;
    }

    friend bool operator!=(const raw_protocol& p1, const raw_protocol& p2) {
      return false;
    }
  };

}
