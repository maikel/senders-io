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

  read_submission::read_submission(mutable_buffer_span buffers, int fd, ::off_t offset) noexcept
    : buffers_{buffers}
    , fd_{fd}
    , offset_{offset} {
  }

  void read_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_READV;
    sqe_.fd = fd_;
    sqe_.off = offset_;
    sqe_.addr = std::bit_cast<__u64>(buffers_.begin());
    sqe_.len = buffers_.size();
    sqe = sqe_;
  }

  read_submission_single::read_submission_single(
    mutable_buffer buffers,
    int fd,
    ::off_t offset) noexcept
    : buffers_{buffers}
    , fd_{fd}
    , offset_{offset} {
  }

  read_submission_single::~read_submission_single() = default;

  void read_submission_single::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_READ;
    sqe_.fd = fd_;
    sqe_.off = offset_;
    sqe_.addr = std::bit_cast<__u64>(buffers_.data());
    sqe_.len = buffers_.size();
    sqe = sqe_;
  }

  write_submission::write_submission(const_buffer_span buffers, int fd, ::off_t offset) noexcept
    : buffers_{buffers}
    , fd_{fd}
    , offset_{offset} {
  }

  void write_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_WRITEV;
    sqe_.fd = fd_;
    sqe_.off = offset_;
    sqe_.addr = std::bit_cast<__u64>(buffers_.begin());
    sqe_.len = buffers_.size();
    sqe = sqe_;
  }

  write_submission_single::write_submission_single(
    const_buffer buffers,
    int fd,
    ::off_t offset) noexcept
    : buffers_{buffers}
    , fd_{fd}
    , offset_{offset} {
  }

  void write_submission_single::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_WRITE;
    sqe_.fd = fd_;
    sqe_.off = offset_;
    sqe_.addr = std::bit_cast<__u64>(buffers_.data());
    sqe_.len = buffers_.size();
    sqe = sqe_;
  }

  write_submission_single::~write_submission_single() = default;
}
