/*
 * Copyright (c) 2021-2022 Facebook, Inc. and its affiliates
 * Copyright (c) 2021-2022 NVIDIA Corporation
 * Copyright (C) 2023 Maikel Nadolski
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

#include <utility>

namespace sio {
  template <auto Next>
  class intrusive_queue;

  template <class Item, Item* Item::* Next>
  class intrusive_queue<Next> {
   public:
    intrusive_queue() noexcept = default;

    intrusive_queue(intrusive_queue&& other) noexcept
      : head_(std::exchange(other.head_, nullptr))
      , tail_(std::exchange(other.tail_, nullptr)) {
    }

    intrusive_queue& operator=(intrusive_queue other) noexcept {
      std::swap(head_, other.head_);
      std::swap(tail_, other.tail_);
      return *this;
    }

    static intrusive_queue make_reversed(Item* list) noexcept {
      Item* new_head = nullptr;
      Item* new_tail = list;
      while (list != nullptr) {
        Item* next = list->*Next;
        list->*Next = new_head;
        new_head = list;
        list = next;
      }

      intrusive_queue result;
      result.head_ = new_head;
      result.tail_ = new_tail;
      return result;
    }

    [[nodiscard]] bool empty() const noexcept {
      return head_ == nullptr;
    }

    [[nodiscard]] Item* front() noexcept {
      return head_;
    }

    [[nodiscard]] Item* pop_front() noexcept {
      Item* item = std::exchange(head_, head_->*Next);
      // This should test if head_ == nullptr, but due to a bug in
      // nvc++'s optimization, `head_` isn't assigned until later.
      // Filed as NVBug#3952534.
      if (item->*Next == nullptr) {
        tail_ = nullptr;
      }
      return item;
    }

    void push_front(Item* item) noexcept {
      item->*Next = head_;
      head_ = item;
      if (tail_ == nullptr) {
        tail_ = item;
      }
    }

    void push_back(Item* item) noexcept {
      item->*Next = nullptr;
      if (tail_ == nullptr) {
        head_ = item;
      } else {
        tail_->*Next = item;
      }
      tail_ = item;
    }

    void append(intrusive_queue other) noexcept {
      if (other.empty())
        return;
      auto* other_head = std::exchange(other.head_, nullptr);
      if (empty()) {
        head_ = other_head;
      } else {
        tail_->*Next = other_head;
      }
      tail_ = std::exchange(other.tail_, nullptr);
    }

    void prepend(intrusive_queue other) noexcept {
      if (other.empty())
        return;

      other.tail_->*Next = head_;
      head_ = other.head_;
      if (tail_ == nullptr) {
        tail_ = other.tail_;
      }

      other.tail_ = nullptr;
      other.head_ = nullptr;
    }

   private:
    Item* head_ = nullptr;
    Item* tail_ = nullptr;
  };
} // namespace sio
