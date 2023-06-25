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
#include "./file_handle.hpp"

namespace sio::io_uring {
  void close_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_CLOSE;
    sqe_.fd = fd_;
    sqe = sqe_;
  }

  open_submission::open_submission(open_data data) noexcept
    : data_{static_cast<open_data&&>(data)} {
  }

  open_submission::~open_submission() = default;

  void open_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_OPENAT;
    sqe_.addr = std::bit_cast<__u64>(data_.path_.c_str());
    sqe_.fd = data_.dirfd_;
    sqe_.open_flags = data_.flags_;
    sqe_.len = data_.mode_;
    sqe = sqe_;
  }

  read_submission::read_submission(
    std::variant<::iovec, std::span<::iovec>> buffers,
    int fd,
    ::off_t offset) noexcept
    : buffers_{buffers}
    , fd_{fd}
    , offset_{offset} {
  }

  read_submission::~read_submission() = default;

  void read_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_READV;
    sqe_.fd = fd_;
    sqe_.off = offset_;
    if (buffers_.index() == 0) {
      sqe_.addr = std::bit_cast<__u64>(std::get_if<0>(&buffers_));
      sqe_.len = 1;
    } else {
      std::span<const ::iovec> buffers = *std::get_if<1>(&buffers_);
      sqe_.addr = std::bit_cast<__u64>(buffers.data());
      sqe_.len = buffers.size();
    }
    sqe = sqe_;
  }

  write_submission::write_submission(
    std::variant<::iovec, std::span<::iovec>> buffers,
    int fd,
    ::off_t offset) noexcept
    : buffers_{buffers}
    , fd_{fd}
    , offset_{offset} {
  }

  write_submission::~write_submission() = default;

  void write_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_WRITEV;
    sqe_.fd = fd_;
    sqe_.off = offset_;
    if (buffers_.index() == 0) {
      sqe_.addr = std::bit_cast<__u64>(std::get_if<0>(&buffers_));
      sqe_.len = 1;
    } else {
      std::span<const ::iovec> buffers = *std::get_if<1>(&buffers_);
      sqe_.addr = std::bit_cast<__u64>(buffers.data());
      sqe_.len = buffers.size();
    }
    sqe = sqe_;
  }
}