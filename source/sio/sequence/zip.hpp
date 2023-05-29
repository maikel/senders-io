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

#include "./sequence_concepts.hpp"

#include "../concepts.hpp"

#include <stdexec/__detail/__intrusive_queue.hpp>

namespace sio {
  namespace zip_ {
    using namespace stdexec;
    template <class ResultVariant>
    struct item_operation_result {
      item_operation_result* next_;
      ResultVariant result_;
      void (*complete_)(item_operation_result*) noexcept = nullptr;
    };

    template <class... Results>
    using item_operation_queues =
      std::tuple<__intrusive_queue<item_operation_result<Results>::next_>...>;

    struct on_stop_requested {
      in_place_stop_source& stop_source_;
      void operator()() const noexcept {
        stop_source_.request_stop();
      }
    };

    template <class Receiver, class ResultTuple, class ErrorsVariant>
    struct operation_base : __immovable {
      using mutexes_t = __mapply<__transform<__mconst<std::mutex>, __q<std::tuple>>, ResultTuple>;
      using queues_t = __mapply<__q<item_operation_queues>, ResultTuple>;
      using on_stop = typename stop_token_of_t<env_of_t<Receiver>>::template callback_type<on_stop_requested>;

      [[no_unique_address]] Receiver receiver_;
      [[no_unique_address]] ErrorsVariant errors_;
      std::mutex stop_mutex_;
      mutexes_t mutexes_;
      queues_t queues_;
      std::atomic<int> n_ready_next_items_{};
      in_place_stop_source stop_source_{};
      std::optional<on_stop> stop_callback_{};
      std::ptrdiff_t n_pending_ops_{};

      bool increase_op_count() noexcept {
        std::scoped_lock lock(stop_mutex_);
        if (stop_source_.stop_requested()) {
          return false;
        }
        n_pending_ops_ += 1;
        return true;
      }

      template <std::size_t Index>
      bool push_back_item_op(
        item_operation_result<std::tuple_element_t<Index, ResultTuple>>* op) noexcept {
        if (increase_op_count()) {
          std::scoped_lock lock(std::get<Index>(mutexes_));
          std::get<Index>(item_queues_).push_back(op);
          return true;
        }
        return false;
      }

      void notify_op_completion() noexcept {
        std::scoped_lock lock(stop_mutex_);
        n_pending_ops -= 1;
        if (n_pending_ops_ == 0 && stop_source_.stop_requested()) {
          auto token = stdexec::get_stop_token(stdexec::get_env(receiver_));
          if (token.stop_requested()) {
            stdexec::set_stopped(static_cast<Receiver&&>(receiver_));
          } else if (errors_.index()) {
            std::visit(
              [&]<class Error>(Error&& error) {
                if constexpr (__not_decays_to<Error, std::monostate>) {
                  stdexec::set_error(static_cast<Receiver&&>(receiver_), (Error&&) error);
                }
              },
              static_cast<ErrorsVariant&&>(__error_));
          } else {
            stdexec::set_value(static_cast<Receiver&&>(receiver_));
          }
        }
      }
    };

    struct zip_t { };
  }

  using zip_::zip_t;
  inline constexpr zip_t zip{};
}