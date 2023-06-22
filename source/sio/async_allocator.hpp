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
    constexpr auto operator()(Alloc alloc, Args&&... args) const
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
    constexpr auto operator()(Alloc alloc, Args&&... args) const
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

  template <class T>
  struct new_delete_allocator {
    using value_type = T;

    constexpr new_delete_allocator() noexcept = default;

    template <class S>
    constexpr explicit new_delete_allocator(const new_delete_allocator<S>&) noexcept {
    }

    auto allocate(allocate_t, std::size_t size) const {
      return stdexec::then(stdexec::just(size), [](std::size_t size) {
        return std::allocator<T>().allocate(size);
      });
    }

    auto deallocate(deallocate_t, T* ptr) const {
      return stdexec::then(stdexec::just(ptr), [](T* ptr) noexcept {
        std::allocator<T>().deallocate(ptr, 1);
      });
    }
  };

  struct get_allocator_t {
    template <class Env>
      requires stdexec::tag_invocable<get_allocator_t, Env>
    auto operator()(const Env& env) const noexcept {
      return stdexec::tag_invoke(*this, env);
    }

    template <class Env>
      requires(!stdexec::tag_invocable<get_allocator_t, Env>)
    new_delete_allocator<char> operator()(const Env& env) const noexcept {
      return new_delete_allocator<char>{};
    }

    auto operator()() const noexcept {
      return stdexec::read(*this);
    }
  };

  inline constexpr get_allocator_t get_allocator{};
}
