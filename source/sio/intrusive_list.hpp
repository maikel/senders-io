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

#include <tuple>
#include <utility>
#include <ranges>

namespace sio {
  template <auto Next>
  struct intrusive_iterator;

  template <class Item, Item* Item::*Next>
  struct intrusive_iterator<Next>
  {
    using difference_type = std::ptrdiff_t;
    Item* item_ = nullptr;

    Item& operator*() const noexcept {
      return *item_;
    }

    Item* operator->() const noexcept {
      return item_;
    }

    intrusive_iterator& operator++() noexcept {
      item_ = item_->*Next;
      return *this;
    }

    intrusive_iterator operator++(int) noexcept {
      intrusive_iterator copy{*this};
      item_ = item_->*Next;
      return copy;
    }

    friend bool operator==(const intrusive_iterator& lhs, const intrusive_iterator& rhs) noexcept {
      return lhs.item_ == rhs.item_;
    }

    friend bool operator!=(const intrusive_iterator& lhs, const intrusive_iterator& rhs) noexcept {
      return lhs.item_ != rhs.item_;
    }
  };

  template <auto Next, auto Prev>
  class intrusive_list;

  template <class Item, Item* Item::*Next, Item* Item::*Prev>
  class intrusive_list<Next, Prev> {
   public:
    intrusive_list() noexcept = default;

    intrusive_list(intrusive_list&& other) noexcept
      : head_(std::exchange(other.head_, nullptr))
      , tail_(std::exchange(other.tail_, nullptr)) {
    }

    intrusive_list& operator=(intrusive_list other) noexcept {
      std::swap(head_, other.head_);
      std::swap(tail_, other.tail_);
      return *this;
    }

    intrusive_iterator<Next> begin() const noexcept {
      return intrusive_iterator<Next>{head_};
    }

    intrusive_iterator<Next> end() const noexcept {
      return intrusive_iterator<Next>{};
    }

    [[nodiscard]] bool empty() const noexcept {
      return head_ == nullptr;
    }

    [[nodiscard]] Item* front() noexcept {
      return head_;
    }

    [[nodiscard]] Item* pop_front() noexcept {
      Item* item = std::exchange(head_, head_->*Next);
      if (item->*Next == nullptr) {
        tail_ = nullptr;
      } else {
        item->*Next->*Prev = nullptr;
      }
      return item;
    }

    void push_front(Item* item) noexcept {
      item->*Next = head_;
      head_ = item;
      if (tail_ == nullptr) {
        tail_ = item;
        head_->*Next->*Prev = head_;
      }
    }

    void push_back(Item* item) noexcept {
      item->*Next = nullptr;
      item->*Prev = tail_;
      if (tail_ == nullptr) {
        head_ = item;
      } else {
        tail_->*Next = item;
      }
      tail_ = item;
    }

    void erase(Item* item) {
      if (item->*Prev == nullptr) {
        head_ = item->*Next;
      } else {
        item->*Prev->*Next = item->*Next;
      }
      if (item->*Next == nullptr) {
        tail_ = item->*Prev;
      } else {
        item->*Next->*Prev = item->*Prev;
      }
    }

    void append(intrusive_list other) noexcept {
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

    void prepend(intrusive_list other) noexcept {
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
