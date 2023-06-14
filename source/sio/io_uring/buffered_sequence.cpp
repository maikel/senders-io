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
#include "./buffered_sequence.hpp"

namespace sio {
  std::size_t advance_buffers(std::variant<::iovec, std::span<::iovec>>& buffers, std::size_t n) noexcept {
    if (::iovec* buffer = std::get_if<0>(&buffers)) {
      STDEXEC_ASSERT(n <= buffer->iov_len);
      buffer->iov_base = static_cast<char*>(buffer->iov_base) + n;
      buffer->iov_len -= n;
    } else {
      std::span<::iovec>* buffers_ = std::get_if<1>(&buffers);
      STDEXEC_ASSERT(buffers_);
      while (n && !buffers_->empty()) {
        if (n >= buffers_->front().iov_len) {
          n -= buffers_->front().iov_len;
          *buffers_ = buffers_->subspan(1);
        } else {
          STDEXEC_ASSERT(n < buffers_->front().iov_len);
          buffers_->front().iov_base = static_cast<char*>(buffers_->front().iov_base) + n;
          buffers_->front().iov_len -= n;
          n = 0;
        }
      }
    }
    return n;
  }

  bool buffers_is_empty(std::variant<::iovec, std::span<::iovec>> buffers) noexcept {
    if (::iovec* buffer = std::get_if<0>(&buffers)) {
      return buffer->iov_len == 0;
    } else {
      std::span<::iovec>* buffers_ = std::get_if<1>(&buffers);
      STDEXEC_ASSERT(buffers_);
      return buffers_->empty();
    }
  }
}