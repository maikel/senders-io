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

//! @file This files contains a deque data structure that is primarily used for work-stealing schedulers
//!
//! This deque is lock-free

#include "./assert.hpp"

#include <atomic>
#include <memory>
#include <vector>

namespace sio {

  enum class steal_status {
    success,
    empty,
    cas_failed
  };

  template <class T>
  struct steal_result {
    T* pointer;
    steal_status status;
  };

  template <class T>
  class deque {
   public:
    deque() = default;

    explicit deque(std::span<std::atomic<T*>> array) noexcept
      : array_(array) {
    }

    bool push_back(T* value) noexcept;

    steal_result<T> try_steal_back() noexcept;

    T* steal_back() noexcept;

    T* pop_front() noexcept;

   private:
    std::atomic<std::size_t> head_ = 0;
    std::atomic<std::size_t> tail_and_generation_ = 0;
    std::atomic<std::shared_ptr<std::atomic<T*>[]>> array_ = nullptr;
    std::size_t capacity_ = 0;
  };

  template <class T>
  T* deque<T>::pop_front() noexcept {
    std::shared_ptr<std::span<std::atomic<T*>>> array = array_.load(std::memory_order_relaxed);
    SIO_ASSERT(array);
    std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t hidx = head % array->size();
    while (expected
           && !array_[hidx].compare_exchange_weak(expected, nullptr, std::memory_order_relaxed))
      ;
    if (expected) {
      head_.store(head + 1, std::memory_order_relaxed);
    }
    return expected;
  }

  template <class T>
  steal_result<T> deque<T>::try_steal_back() noexcept {
    std::shared_ptr<std::atomic<T*>[]> array = array_.load(std::memory_order_acquire);
    std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t tail_and_gen = tail_and_generation_.load(std::memory_order_relaxed);
    std::size_t tail = tail_and_gen >> 1;
    unsigned char generation = tail_and_gen & 1;
    std::size_t size = tail - head;
    if (size == 0) {
      return {nullptr, steal_status::empty};
    }
    std::size_t tidx = static_cast<std::size_t>(tail - 1) % array->size();
    T* expected = array_[tidx].load(std::memory_order_relaxed);
    if (expected) {
      if (!array[tidx].compare_exchange_strong(expected, nullptr, std::memory_order_relaxed)) {
        return {nullptr, steal_status::cas_failed};
      }
      std::size_t new_tail_and_gen = ((tail - 1) << 1) | generation;
      tail_.compare_exchange_strong(
        tail_and_gen, new_tail_and_gen, std::memory_order_release, std::memory_order_relaxed);
    } else {
      return {nullptr, steal_status::cas_failed};
    }
    return {expected, steal_status::success};
  }

  template <class T>
  T* deque<T>::steal_back() noexcept {
    steal_result<T> result = try_steal_back();
    while (result.status == steal_status::cas_failed) {
      result = try_steal_back();
    }
    return result.pointer;
  }

  template <class T>
  bool deque<T>::push_back(T* value) noexcept {
    std::shared_ptr<std::atomic<T*>[]> array = array_.load(std::memory_order_relaxed);
    std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t tail_and_gen = tail_and_generation_.load(std::memory_order_acquire);
    std::size_t tail = tail_and_gen >> 1;
    unsigned char generation = tail & 1;
    std::size_t size = tail - head;
    if (size == capacity_) {
      capacity_ = 2 * size;
      SIO_ASSERT(capacity_ > size);
      std::shared_ptr<std::atomic<T*>[]> new_array =
        std::make_shared_for_overwrite<std::atomic<T*>[]>(capacity_);
      std::size_t new_size = 0;
      for (std::size_t i = head; i < tail; ++i) {
        std::size_t idx = i % size;
        T* obj = array[idx].exchange(nullptr, std::memory_order_relaxed);
        if (obj) {
          new_array[idx].store(obj, std::memory_order_relaxed);
          new_size += 1;
        }
      }
      array_.store(new_array, std::memory_order_relaxed);
      tail = head + new_size;
      generation = (generation + 1) & 1;
      tail_and_gen = (new_tail << 1) | new_generation;
      tail_.store(tail_and_gen, std::memory_order_release);
    }
    std::size_t tidx = tail % capacity_;
    T* expected = nullptr;
    while (!array_[tidx].compare_exchange_strong(expected, value, std::memory_order_relaxed)) {
      tail_and_gen = tail_and_generation_.load(std::memory_order_acquire);
      tidx = tail % array_.size();
      expected = nullptr;
    }
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }
}
