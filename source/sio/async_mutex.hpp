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

#include <exec/__detail/__atomic_intrusive_queue.hpp>
#include <exec/any_sender_of.hpp>

#include <atomic>

namespace sio {
  namespace mutex_ {
    struct operation_base {
      operation_base* next = nullptr;
      void (*complete)(operation_base*) noexcept = nullptr;
    };

    struct base {
      std::atomic<bool> locked_{false};
      exec::__atomic_intrusive_queue<&operation_base::next> inflight_operations_{};
    };

    template <class R>
    struct lock_operation_base {
      R rcvr_;
    };

    template <class Receiver>
    struct lock_operation
      : lock_operation_base<Receiver>
      , operation_base {

      base& base_;

      static void on_complete(operation_base* op) noexcept {
        auto* self = static_cast<lock_operation*>(op);
        stdexec::set_value(std::move(self->rcvr_));
      }

      lock_operation(base& base, Receiver receiver)
        : lock_operation_base<Receiver>{std::move(receiver)}
        , operation_base{nullptr, &on_complete}
        , base_{base} {
      }

      void start() noexcept {
        base& mutex = base_;
        mutex.inflight_operations_.push_front(this);
        bool expected_lock = false;
        while (
          mutex.locked_.compare_exchange_strong(expected_lock, true, std::memory_order_acq_rel)) {
          auto pending_ops = mutex.inflight_operations_.pop_all();
          while (!pending_ops.empty()) {
            do {
              operation_base* next = pending_ops.pop_front();
              next->complete(next);
            } while (!pending_ops.empty());
            pending_ops = mutex.inflight_operations_.pop_all();
          }
          expected_lock = false;
          mutex.locked_.store(false, std::memory_order_release);
          if (mutex.inflight_operations_.empty()) {
            break;
          }
        }
      }
    };

    struct lock_sender {
      using sender_concept = stdexec::sender_t;
      using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t()>;

      base* mutex_;

      template <class Receiver>
      auto connect(Receiver receiver) noexcept -> lock_operation<Receiver> {
        return {*mutex_, std::move(receiver)};
      }
    };
  }

  struct async_mutex : mutex_::base {
    mutex_::lock_sender lock() noexcept {
      return {this};
    }
  };
}
