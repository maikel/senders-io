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

#include "./const_buffer.hpp"

#include <span>

namespace sio {
  class const_buffer_span;

  class const_buffer_subspan {
    const const_buffer* buffers_{nullptr};
    std::size_t size_{};
    std::size_t i0_{};
    std::size_t iN_{};

    explicit const_buffer_subspan(
      const const_buffer* pointer,
      std::size_t size,
      std::size_t i0,
      std::size_t iN) noexcept;

   public:
    const_buffer_subspan() = default;

    explicit const_buffer_subspan(const const_buffer_span& span) noexcept;

    class const_iterator {
      const const_buffer_subspan* parent_{nullptr};
      std::size_t index_{};

      friend class const_buffer_subspan;
      explicit const_iterator(const const_buffer_subspan* parent, std::size_t index = 0) noexcept;
     public:
      [[nodiscard]] const_buffer operator*() const noexcept;

      const_iterator& operator++() noexcept;

      const_iterator operator++(int) noexcept;

      const_iterator& operator--() noexcept;

      const_iterator operator--(int) noexcept;

      friend auto operator<=>(const_iterator, const_iterator) = default;
    };

    [[nodiscard]] bool empty() const noexcept {
      return size_ == 0;
    }

    [[nodiscard]] std::size_t size() const noexcept {
      return size_;
    }

    [[nodiscard]] std::size_t buffer_size() const noexcept;

    [[nodiscard]] const_iterator begin() const noexcept {
      return const_iterator{this};
    }

    [[nodiscard]] const_iterator end() const noexcept {
      return const_iterator{this, size_};
    }

    [[nodiscard]] const_buffer_subspan prefix(std::size_t n) const noexcept;

    [[nodiscard]] const_buffer_subspan suffix(std::size_t n) const noexcept;

    friend auto operator<=>(const const_buffer_subspan&, const const_buffer_subspan&) = default;
  };

  class const_buffer_span {
    const const_buffer* buffers_{nullptr};
    std::size_t size_{};

   public:
    using value_type = const_buffer;
    using const_iterator = const value_type*;

    const_buffer_span() = default;

    explicit const_buffer_span(std::span<const const_buffer> buffers)
      : buffers_{buffers.data()}
      , size_{buffers.size()} {
    }

    explicit const_buffer_span(const const_buffer* pointer, std::size_t size)
      : buffers_{pointer}
      , size_{size} {
    }

    [[nodiscard]] bool empty() const noexcept {
      return size_ == 0;
    }

    [[nodiscard]] std::size_t size() const noexcept {
      return size_;
    }

    [[nodiscard]] const_iterator begin() const noexcept {
      return buffers_;
    }

    [[nodiscard]] const_iterator end() const noexcept {
      return buffers_ + size_;
    }

    [[nodiscard]] std::size_t buffer_size() const noexcept {
      const_buffer_subspan subspan{*this};
      return subspan.buffer_size();
    }

    [[nodiscard]] const_buffer_subspan prefix(std::size_t n) const noexcept {
      const_buffer_subspan subspan{*this};
      return subspan.prefix(n);
    }

    [[nodiscard]] const_buffer_subspan suffix(std::size_t n) const noexcept {
      const_buffer_subspan subspan{*this};
      return subspan.suffix(n);
    }

    friend auto operator<=>(const_buffer_span, const_buffer_span) = default;
  };
}