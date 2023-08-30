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

  struct async_new_t;
  extern const async_new_t async_new;

  namespace async_new_ {
    template <class Alloc, class... Args>
    concept has_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.async_new(async_new, static_cast<Args&&>(args)...) };
    };

    template <class Alloc, class... Args>
    concept nothrow_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.async_new(async_new, static_cast<Args&&>(args)...) } noexcept;
    };
  }

  struct async_new_t {
    template <class Alloc, class... Args>
      requires async_new_::has_member_cust<Alloc, Args...>
    constexpr auto operator()(Alloc alloc, Args&&... args) const
      noexcept(async_new_::nothrow_member_cust<Alloc, Args...>) {
      return alloc.async_new(async_new, static_cast<Args&&>(args)...);
    }
  };

  inline constexpr async_new_t async_new{};

  struct async_new_array_t;
  extern const async_new_array_t async_new_array;

  namespace async_new_array_ {
    template <class Alloc, class... Args>
    concept has_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.async_new_array(async_new_array, static_cast<Args&&>(args)...) };
    };

    template <class Alloc, class... Args>
    concept nothrow_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.async_new_array(async_new_array, static_cast<Args&&>(args)...) } noexcept;
    };
  }

  struct async_new_array_t {
    template <class Alloc, class... Args>
      requires async_new_array_::has_member_cust<Alloc, Args...>
    constexpr auto operator()(Alloc alloc, Args&&... args) const
      noexcept(async_new_array_::nothrow_member_cust<Alloc, Args...>) {
      return alloc.async_new_array(async_new_array, static_cast<Args&&>(args)...);
    }
  };

  inline constexpr async_new_array_t async_new_array{};

  struct async_delete_t;
  extern const async_delete_t async_delete;

  namespace async_delete_ {
    template <class Alloc, class... Args>
    concept has_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.async_delete(async_delete, static_cast<Args&&>(args)...) };
    };

    template <class Alloc, class... Args>
    concept nothrow_member_cust = requires(Alloc alloc, Args&&... args) {
      { alloc.async_delete(async_delete, static_cast<Args&&>(args)...) } noexcept;
    };
  }

  struct async_delete_t {
    template <class Alloc, class... Args>
      requires async_delete_::has_member_cust<Alloc, Args...>
    constexpr auto operator()(Alloc alloc, Args&&... args) const
      noexcept(async_delete_::nothrow_member_cust<Alloc, Args...>) {
      return alloc.async_delete(async_delete, static_cast<Args&&>(args)...);
    }
  };

  inline constexpr async_delete_t async_delete{};

  template <class Alloc, class T, class... Args>
  concept allocator = //
    requires(Alloc alloc, T* ptr, Args&&... args) {
      { async_new(alloc, static_cast<Args&&>(args)...) };
      { async_delete(alloc, ptr) };
    };

  template <class Alloc, class T>
  concept array_allocator = //
    requires(Alloc alloc, std::size_t size, T* ptr) {
      { async_new_array(alloc, size) };
      { async_delete(alloc, ptr) };
    };

  template <class T, class Receiver>
  struct delete_operation {
    Receiver rcvr_;
    T* pointer_;

    void start(stdexec::start_t) noexcept {
      Receiver rcvr = static_cast<Receiver&&>(rcvr_);
      T* pointer = pointer_;
      std::destroy_at(pointer);
      // We might have detroyed this operation itself.
      // dont touch any member variables anymore
      std::allocator<T>().deallocate(pointer, 1);
      stdexec::set_value(static_cast<Receiver&&>(rcvr));
    }
  };

  template <class T>
  struct delete_sender {
    using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t()>;

    T* pointer_;

    template <class Receiver>
    delete_operation<T, Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return {static_cast<Receiver&&>(rcvr), pointer_};
    }
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

    template <class... Args>
    auto async_new(async_new_t, Args&&... args) const {
      return stdexec::then(
        stdexec::just(static_cast<Args&&>(args)...), []<class... As>(As&&... args) {
          std::allocator<T> alloc{};
          T* ptr = alloc.allocate(1);
          try {
            return new (ptr) T(static_cast<As&&>(args)...);
          } catch (...) {
            alloc.deallocate(ptr, 1);
            throw;
          }
        });
    }

    auto async_new_array(async_new_array_t, std::size_t size) const {

      return stdexec::then(stdexec::just(size), [](std::size_t size) { return new T[size]; });
    }

    delete_sender<T> async_delete(async_delete_t, T* ptr) const {
      return {ptr};
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
