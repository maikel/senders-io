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

#include <cstddef>
#include <span>

namespace sio {
  class const_buffer {
    const std::byte* data_{nullptr};
    std::size_t size_{};

   public:
    const_buffer() = default;

    explicit const_buffer(std::span<const std::byte> data) noexcept
      : data_{data.data()}
      , size_{data.size()} {
    }

    explicit const_buffer(const void* pointer, std::size_t size) noexcept
      : data_{static_cast<const std::byte*>(pointer)}
      , size_{size} {
    }

    const std::byte* data() const noexcept {
      return data_;
    }

    const std::size_t size() const noexcept {
      return size_;
    }

    bool empty() const noexcept {
      return size_ == 0;
    }

    const_buffer& operator+=(std::size_t n) noexcept {
      if (n >= size_) {
        data_ += size_;
        size_ = 0;
        return *this;
      }
      data_ += n;
      size_ -= n;
      return *this;
    }

    friend const_buffer operator+(const_buffer lhs, std::size_t rhs) noexcept {
      lhs += rhs;
      return lhs;
    }

    friend const_buffer operator+(std::size_t lhs, const_buffer rhs) noexcept {
      rhs += lhs;
      return rhs;
    }

    [[nodiscard]] const_buffer prefix(std::size_t n) const noexcept {
      if (n >= size_) {
        return *this;
      }
      return const_buffer{data_, n};
    }

    [[nodiscard]] const_buffer suffix(std::size_t n) const noexcept {
      if (n >= size_) {
        return *this;
      }
      return const_buffer{data_ + size_ - n, n};
    }
  };
}
