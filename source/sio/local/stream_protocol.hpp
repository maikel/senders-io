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

#include <sys/socket.h>

#include "./endpoint.hpp"

namespace sio::local {

  class stream_protocol {
   public:
    using endpoint = local::endpoint;

    int type() const {
      return SOCK_STREAM;
    }

    int protocol() const {
      return 0;
    }

    int family() const {
      return AF_LOCAL;
    }

    friend bool operator==(const stream_protocol& p1, const stream_protocol& p2) {
      return true;
    }

    friend bool operator!=(const stream_protocol& p1, const stream_protocol& p2) {
      return false;
    }
  };

}
