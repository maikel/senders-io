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

#include <sys/un.h>

#include <cstring>
#include <string_view>

namespace sio::local {
  class endpoint {
   public:
    using native_handle_type = ::sockaddr_un;

    explicit endpoint(std::string_view path)
    : addr_{.sun_family = AF_LOCAL}
    {
      auto len = std::min(path.size(), sizeof(addr_.sun_path) - 1);
      std::memcpy(addr_.sun_path, path.data(), len);
      addr_.sun_path[len] = '\0';
    }

    const ::sockaddr_un* data() const noexcept {
      return &addr_;
    }

    ::size_t size() const noexcept {
      return sizeof(addr_);
    }

    std::string_view path() const noexcept {
      return std::string_view{&addr_.sun_path[0]};
    }

   private:
    ::sockaddr_un addr_{.sun_family = AF_LOCAL};
  };
}