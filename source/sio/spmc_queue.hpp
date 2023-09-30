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

//! @file This files contains a spmc_queue data structure that is primarily used for work-stealing schedulers
//!
//! This spmc_queue is lock-free

#include "./assert.hpp"

#include <atomic>
#include <span>

namespace sio {
  template <class T>
  class spmc_queue {
   public:
    spmc_queue() = default;

    explicit spmc_queue(std::span<std::atomic<T*>> array) noexcept
      : array_(array) {
    }

    bool empty() const noexcept;

    bool push_back(T* value) noexcept;

    T* pop_front() noexcept;

   private:
    std::atomic<std::size_t> head_ = 0;
    std::size_t tail_ = 0;
    std::span<std::atomic<T*>> array_{};
  };

  template <class T>
  T* spmc_queue<T>::pop_front() noexcept {
    std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t hidx = head % array_.size();
    T* expected = array_[hidx].exchange(nullptr, std::memory_order_relaxed);
    if (expected) {
      std::size_t next = head + std::size_t{1};
      [[maybe_unused]] bool success = head_.compare_exchange_strong(
        head, next, std::memory_order_relaxed);
      SIO_ASSERT(success);
    }
    return expected;
  }

  template <class T>
  bool spmc_queue<T>::push_back(T* value) noexcept {
    std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t size = tail_ - head;
    std::size_t capacity = array_.size();
    if (size < capacity) {
      std::size_t tidx = tail_ % capacity;
      array_[tidx].store(value, std::memory_order_relaxed);
      tail_ = tail_ + std::size_t{1};
      return true;
    }
    return false;
  }

  template <class T>
  bool spmc_queue<T>::empty() const noexcept {
    std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t tail = tail_;
    return head == tail;
  }
}
