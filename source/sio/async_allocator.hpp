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

#include "./concepts.hpp"

namespace sio::async {
  struct allocate_t;
  extern const allocate_t allocate;
  namespace allocate_ {
    template <class Alloc, class... Args>
    concept has_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.allocate(allocate, static_cast<Args&&>(args)...) };
    };

    template <class Alloc, class... Args>
    concept nothrow_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.allocate(allocate, static_cast<Args&&>(args)...) } noexcept;
    };
  }

  struct allocate_t {
    template <class Alloc, class... Args>
    requires allocate_::has_member_cust<Alloc, Args...>
    constexpr auto operator()(Alloc& alloc, Args&&... args) const
      noexcept(allocate_::nothrow_member_cust<Alloc, Args...>) {
      return alloc.allocate(allocate, static_cast<Args&&>(args)...);
    }
  };

  inline constexpr allocate_t allocate{};

  struct deallocate_t;
  extern const deallocate_t deallocate;

  namespace deallocate_ {
    template <class Alloc, class... Args>
    concept has_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.deallocate(deallocate, static_cast<Args&&>(args)...) };
    };

    template <class Alloc, class... Args>
    concept nothrow_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.deallocate(deallocate, static_cast<Args&&>(args)...) } noexcept;
    };
  }

  struct deallocate_t {
    template <class Alloc, class... Args>
    requires deallocate_::has_member_cust<Alloc, Args...>
    constexpr auto operator()(Alloc& alloc, Args&&... args) const
      noexcept(deallocate_::nothrow_member_cust<Alloc, Args...>) {
      return alloc.deallocate(deallocate, static_cast<Args&&>(args)...);
    }
  };

  inline constexpr deallocate_t deallocate{};


  template <class Alloc>
  concept allocator = requires(Alloc alloc, std::size_t size, void* ptr) {
    { allocate(alloc, size) };
    { deallocate(alloc, ptr) };
  };
}

