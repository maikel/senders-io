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

#include <optional>

namespace sio {
  namespace merge_each_ {
    template <class Receiver, class ErrorsVariant>
    struct operation_base {
      std::atomic<int> n_pending_ops_;
      [[no_unique_address]] Receiver receiver_;
      [[no_unique_address]] ErrorsVariant errors_{};
      std::atomic<bool> error_emplaced_{false};
      stdexec::in_place_stop_source stop_source_{};
      // std::optional<default_stop_callback_t> on_receiver_stopped_{};
    };

    template <class Receiver, class ErrorsVariant>
    struct receiver {
      using is_receiver = void;

      operation_base<Receiver, ErrorsVariant>* op_;

      template <class Item>
        requires callable<exec::set_next_t, Receiver&, Item>
      auto set_next(exec::set_next_t, Item&& item) {
        return exec::set_next(op_->receiver_, static_cast<Item&&>(item));
      }

      void complete() noexcept {
        if (op_->n_pending_ops_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          // op_->on_receiver_stopped_.reset();
          bool error_emplaced = op_->error_emplaced_.load(std::memory_order_acquire);
          if (error_emplaced) {
            stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), std::move(op_->errors_));
          } else {
            exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->receiver_));
          }
        }
      }

      void set_value(stdexec::set_value_t) && noexcept {
        complete();
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->stop_source_.request_stop();
        complete();
      }

      template <class Error>
        requires callable<stdexec::set_error_t, Receiver&&, Error> //
              && emplaceable<ErrorsVariant, decay_t<Error>, Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        if (!op_->error_emplaced_.exchange(true, std::memory_order_relaxed)) {
          op_->errors_.template emplace<decay_t<Error>>(static_cast<Error&&>(error));
          std::atomic_thread_fence(std::memory_order_release);
        }
        op_->stop_source_.request_stop();
        complete();
      }

      auto get_env(stdexec::get_env_t) const noexcept {
        return exec::make_env(
          stdexec::get_env(op_->receiver_),
          exec::with(stdexec::get_stop_token, op_->stop_source_.get_token()));
      }
    };

    template <class Receiver, class... Senders>
    struct traits {
      using env = stdexec::env_of_t<Receiver>;

      using errors_variant = //
        stdexec::__minvoke<
          stdexec::__mconcat<stdexec::__nullable_variant_t>,
          stdexec::error_types_of_t<Senders, env, stdexec::__types>...>;

      using receiver_t = receiver<Receiver, errors_variant>;
    };

    template <class Receiver, class... Senders>
    struct operation
      : operation_base<Receiver, typename traits<Receiver, Senders...>::errors_variant> {
      using base_t =
        operation_base<Receiver, typename traits<Receiver, Senders...>::errors_variant>;
      using receiver_t = typename traits<Receiver, Senders...>::receiver_t;

      std::tuple<exec::subscribe_result_t<Senders, receiver_t>...> ops_;

      operation(Receiver rcvr, Senders&&... sndrs)
        : base_t{sizeof...(Senders), static_cast<Receiver&&>(rcvr)}
        , ops_{stdexec::__conv{[&] {
          return exec::subscribe(static_cast<Senders&&>(sndrs), receiver_t{this});
        }}...} {
      }

      void start(stdexec::start_t) noexcept {
        std::apply([](auto&... op) { (stdexec::start(op), ...); }, ops_);
      }
    };

    template <class... Senders>
    struct sender {
      using is_sender = exec::sequence_tag;

      std::tuple<Senders...> senders_;

      template <decays_to<sender> Self, class Receiver>
      static auto subscribe(Self&& self, exec::subscribe_t, Receiver receiver) {
        return std::apply(
          [&]<class... Sndrs>(Sndrs&&... sndrs) {
            return operation<Receiver, Sndrs...>{
              static_cast<Receiver&&>(receiver), static_cast<Sndrs&&>(sndrs)...};
          },
          static_cast<Self&&>(self).senders_);
      }

      template <decays_to<sender> Self, class Env>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> stdexec::__concat_completion_signatures_t<
          stdexec::completion_signatures_of_t<copy_cvref_t<Self, Senders>, Env>...>;
    };

    struct merge_each_t {
      template <class... Senders>
      auto operator()(Senders&&... senders) const noexcept((nothrow_decay_copyable<Senders> && ...))
        -> sender<decay_t<Senders>...> {
        return {{static_cast<Senders&&>(senders)...}};
      }
    };
  }

  using merge_each_::merge_each_t;
  inline constexpr merge_each_t merge_each{};
}