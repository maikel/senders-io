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

#include <cstdint>
#include <cstddef>

namespace sio {
  class mutable_buffer {
    const std::byte* data_{nullptr};
    std::size_t size_{};

   public:
    mutable_buffer() = default;

    explicit mutable_buffer(const void* pointer, std::size_t size)
      : data_{static_cast<const std::byte*>(pointer)}
      , size_{size} {
    }

    const std::byte* data() const noexcept {
      return data_;
    }

    const std::size_t size() const noexcept {
      return size_;
    }

    mutable_buffer& operator+=(std::size_t n) noexcept {
      if (n >= size_) {
        data_ += size_;
        size_ = 0;
        return *this;
      }
      data_ += n;
      size_ -= n;
      return *this;
    }

    friend mutable_buffer operator+(mutable_buffer lhs, std::size_t rhs) noexcept {
      lhs += rhs;
      return lhs;
    }

    friend mutable_buffer operator+(std::size_t lhs, mutable_buffer rhs) noexcept {
      rhs += lhs;
      return rhs;
    }

    [[nodiscard]] mutable_buffer prefix(std::size_t n) const noexcept {
      if (n >= size_) {
        return *this;
      }
      return mutable_buffer{data_, n};
    }

    [[nodiscard]] mutable_buffer suffix(std::size_t n) const noexcept {
      if (n >= size_) {
        return *this;
      }
      return mutable_buffer{data_ + size_ - n, n};
    }
  };
}