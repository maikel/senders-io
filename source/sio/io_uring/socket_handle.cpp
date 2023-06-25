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

#include "./socket_handle.hpp"

namespace sio::io_uring {
  namespace connect_ {
    void submission::submit(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CONNECT;
      sqe_.fd = fd_;
      sqe_.addr = std::bit_cast<__u64>(peer_endpoint_.data());
      sqe_.off = peer_endpoint_.size();
      sqe = sqe_;
    }
  }

  void accept_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_ACCEPT;
    sqe_.fd = fd_;
    sqe_.addr = std::bit_cast<__u64>(local_endpoint_.data());
    sqe_.addr2 = std::bit_cast<__u64>(&addrlen);
    sqe = sqe_;
  }
}