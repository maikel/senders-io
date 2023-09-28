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
#include "./const_buffer_span.hpp"

namespace sio {
  const_buffer_subspan::const_buffer_subspan(const const_buffer_span& span) noexcept
    : buffers_{span.begin()}
    , size_{span.size()} {
    if (size_ > 0) {
      const const_buffer* last = buffers_ + size_ - 1;
      iN_ = last->size();
    }
  }

  const_buffer_subspan::const_buffer_subspan(
    const const_buffer* pointer,
    std::size_t size,
    std::size_t i0,
    std::size_t iN) noexcept
    : buffers_{pointer}
    , size_{size}
    , i0_{i0}
    , iN_{iN} {
    if (size_ == 1 && i0_ == iN_) {
      buffers_ = nullptr;
      size_ = 0;
    }
  }

  const_buffer_subspan::const_iterator::const_iterator(
    const const_buffer_subspan* parent,
    std::size_t index) noexcept
    : parent_{parent}
    , index_{index} {
  }

  const_buffer const_buffer_subspan::const_iterator::operator*() const noexcept {
    const std::size_t size = parent_->size_;
    constexpr const std::size_t last = std::size_t(-1);
    std::size_t index = index_ >= size - 1 ? last : index_;
    switch (index) {
    [[likely]] case 0:
      return parent_->buffers_[index] + parent_->i0_;
    case last:
      return parent_->buffers_[index].prefix(parent_->iN_);
    default:
      return parent_->buffers_[index];
    }
  }

  const_buffer_subspan::const_iterator&
    const_buffer_subspan::const_iterator::operator++() noexcept {
    ++index_;
    return *this;
  }

  const_buffer_subspan::const_iterator
    const_buffer_subspan::const_iterator::operator++(int) noexcept {
    const_iterator tmp = *this;
    ++*this;
    return tmp;
  }

  const_buffer_subspan::const_iterator&
    const_buffer_subspan::const_iterator::operator--() noexcept {
    --index_;
    return *this;
  }

  const_buffer_subspan::const_iterator
    const_buffer_subspan::const_iterator::operator--(int) noexcept {
    const_iterator tmp = *this;
    --*this;
    return tmp;
  }

  namespace {
    std::array<std::size_t, 2>
      find_buffer_index_for_n(std::span<const const_buffer> buffers, std::size_t n) noexcept {
      std::size_t i = 0;
      for (const const_buffer& buffer: buffers) {
        if (n <= buffer.size()) {
          return {i, n};
        }
        n -= buffer.size();
        ++i;
      }
      return {i - 1, buffers.back().size()};
    }
  }

  std::size_t const_buffer_subspan::buffer_size() const noexcept {
    if (size_ == 0) {
      return 0;
    }
    if (size_ == 1) {
      return iN_ - i0_;
    }
    std::size_t size = buffers_->size() - i0_;
    std::span<const const_buffer> buffers{buffers_ + 1, size_ - 2};
    for (const const_buffer& buffer: buffers) {
      size += buffer.size();
    }
    size += iN_;
    return size;
  }

  const_buffer_subspan const_buffer_subspan::prefix(std::size_t n) const noexcept {
    if (size_ == 0) {
      return *this;
    }
    std::size_t first_length = buffers_->size() - i0_;
    if (n <= first_length) {
      return const_buffer_subspan{buffers_, 1, i0_, n + i0_};
    }
    n -= first_length;
    std::span<const const_buffer> buffers{buffers_ + 1, size_ - 1};
    const auto [index, rest] = find_buffer_index_for_n(buffers, n);
    return const_buffer_subspan{buffers_, index + 2, i0_, rest};
  }

  const_buffer_subspan const_buffer_subspan::suffix(std::size_t n) const noexcept {
    if (size_ == 0) {
      return *this;
    }
    if (size_ == 1) {
      const std::size_t length = iN_ - i0_;
      if (n <= length) {
        return const_buffer_subspan{buffers_, 1, i0_ + length - n, iN_};
      }
      return const_buffer_subspan{buffers_, 1, i0_, iN_};
    }
    if (n <= iN_) {
      const const_buffer* last_buffer = buffers_ + size_ - 1;
      return const_buffer_subspan{last_buffer, 1, iN_ - n, iN_};
    }
    n -= iN_;
    std::size_t i = size_ - 2;
    while (i > 0) {
      const const_buffer& buffer = buffers_[i];
      std::size_t length = buffers_->size();
      if (n <= length) {
        const std::size_t rest = length - n;
        return const_buffer_subspan{buffers_ + i, size_ - i, rest, length};
      }
      n -= buffer.size();
      --i;
    }
    std::size_t first_length = buffers_->size() - i0_;
    if (n <= first_length) {
      return const_buffer_subspan{buffers_, size_, i0_ + first_length - n, iN_};
    }
    return *this;
  }
}