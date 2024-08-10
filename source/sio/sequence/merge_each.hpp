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

#include <stdexec/execution.hpp>
#include <exec/__detail/__basic_sequence.hpp>
#include <exec/sequence_senders.hpp>

namespace sio {
  namespace merge_each_ {
    template <class Receiver, class ErrorsVariant>
    struct operation_base {
      std::atomic<int> n_pending_ops_;
      [[no_unique_address]] Receiver receiver_;
      [[no_unique_address]] ErrorsVariant errors_{};
      std::atomic<int> error_emplaced_{0};
      stdexec::inplace_stop_source stop_source_{};
    };

    template <class Receiver>
    struct error_visitor {
      Receiver* receiver_;

      template <class Error>
      void operator()(Error&& error) const noexcept {
        if constexpr (stdexec::__not_decays_to<Error, std::monostate>) {
          stdexec::set_error(static_cast<Receiver&&>(*receiver_), static_cast<Error&&>(error));
        }
      }
    };

    template <class Receiver, class ErrorsVariant>
    struct receiver {
      using receiver_concept = stdexec::receiver_t;

      operation_base<Receiver, ErrorsVariant>* op_;

      template <class Item>
        requires callable<exec::set_next_t, Receiver&, Item>
      friend auto tag_invoke(exec::set_next_t, receiver& self, Item&& item)
        -> exec::next_sender_of_t< Receiver, Item> {
        return exec::set_next(self.op_->receiver_, static_cast<Item&&>(item));
      }

      void complete() noexcept {
        if (op_->n_pending_ops_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          int error_emplaced = op_->error_emplaced_.load(std::memory_order_acquire);
          if (error_emplaced == 2) {
            std::visit(
              error_visitor<Receiver>{&op_->receiver_}, static_cast<ErrorsVariant&&>(op_->errors_));
          } else {
            exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->receiver_));
          }
        }
      }

      void set_value() && noexcept {
        complete();
      }

      void set_stopped() && noexcept {
        op_->stop_source_.request_stop();
        complete();
      }

      template <class Error>
        requires callable<stdexec::set_error_t, Receiver&&, Error> //
              && emplaceable<ErrorsVariant, decay_t<Error>, Error>
      void set_error(Error&& error) && noexcept {
        int expected = 0;
        if (op_->error_emplaced_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          op_->errors_.template emplace<decay_t<Error>>(static_cast<Error&&>(error));
          op_->error_emplaced_.store(2, std::memory_order_release);
        }
        op_->stop_source_.request_stop();
        complete();
      }

      auto get_env() const noexcept {
        return exec::make_env(
          stdexec::get_env(op_->receiver_),
          exec::with(stdexec::get_stop_token, op_->stop_source_.get_token()));
      }
    };

    template <class Receiver, class... Senders>
    struct traits {
      using errors_variant = //
        stdexec::__minvoke<
          stdexec::__mconcat<stdexec::__q<stdexec::__nullable_std_variant>>,
          stdexec::error_types_of_t<Senders, stdexec::env_of_t<Receiver>, stdexec::__types>...>;

      using receiver_t = merge_each_::receiver<Receiver, errors_variant>;
    };

    template <class Receiver, class... Senders>
    struct operation
      : operation_base<Receiver, typename traits<Receiver, Senders...>::errors_variant> {
      using base_type =
        operation_base<Receiver, typename traits<Receiver, Senders...>::errors_variant>;
      using receiver_t = typename traits<Receiver, Senders...>::receiver_t;

      std::tuple<exec::subscribe_result_t<Senders, receiver_t>...> ops_;

      operation(Receiver rcvr, Senders&&... sndrs)
        : base_type{sizeof...(Senders), static_cast<Receiver&&>(rcvr)}
        , ops_{stdexec::__emplace_from{[&] {
          return exec::subscribe(static_cast<Senders&&>(sndrs), receiver_t{this});
        }}...} {
      }

      void start() noexcept {
        std::apply([](auto&... op) { (stdexec::start(op), ...); }, ops_);
      }
    };


    template <class Receiver, class... Sequence>
    concept all_sender_to = ((exec::sequence_sender_to< Sequence, Receiver>) && ...);

    template <class Receiver>
    struct subscribe_fn {
      Receiver& rcvr_;

      template <class... Senders>
        requires all_sender_to<typename traits<Receiver, Senders...>::receiver_t, Senders...>
      auto operator()(stdexec::__ignore, stdexec::__ignore, Senders&&... sequence)
        -> operation<Receiver, Senders...> {
        return {static_cast<Receiver&&>(rcvr_), static_cast<Senders&&>(sequence)...};
      }
    };

    struct merge_each_t {
      template <stdexec::sender... Senders>
        requires stdexec::__domain::__has_common_domain<Senders...>
      auto operator()(Senders&&... senders) const
        noexcept((nothrow_decay_copyable<Senders> && ...)) -> stdexec::__well_formed_sender auto {
        auto domain = stdexec::__domain::__common_domain_t<Senders...>();
        return stdexec::transform_sender(
          domain,
          exec::make_sequence_expr<merge_each_t>(
            stdexec::__{}, static_cast<Senders&&>(senders)...));
      }

      auto operator()() const noexcept -> binder_back<merge_each_t> {
        return {{}, {}, {}};
      }

      template <stdexec::sender_expr_for<merge_each_t> Self, class Receiver>
      static auto subscribe(Self&& self, Receiver rcvr) noexcept(
        stdexec::__nothrow_callable<stdexec::__sexpr_apply_t, Self, subscribe_fn< Receiver>>)
        -> stdexec::__call_result_t<stdexec::__sexpr_apply_t, Self, subscribe_fn< Receiver>> {
        return stdexec::__sexpr_apply(static_cast<Self&&>(self), subscribe_fn<Receiver>{rcvr});
      }

      template <class Env, class... Senders>
      using completions = stdexec::__concat_completion_signatures<
        exec::__sequence_completion_signatures_of_t<Senders, Env>...>;

      template <class Self, class Env>
      using completions_t =
        stdexec::__children_of<Self, stdexec::__mbind_front_q<completions, Env>>;

      template <stdexec::sender_expr_for<merge_each_t> Self, class Env>
      static auto get_completion_signatures(Self&&, Env&&) noexcept -> completions_t<Self, Env>;

      // BUG: support multiply item senders
      template <class Env, class... Senders>
      using item_types = stdexec::__minvoke<
        stdexec::__mconcat<stdexec::__q<exec::item_types>>,
        exec::item_types_of_t<stdexec::__mfront<Senders...>, Env>>;

      template <class Self, class Env>
      using item_types_t = stdexec::__children_of<Self, stdexec::__mbind_front_q<item_types, Env>>;

      template <stdexec::sender_expr_for<merge_each_t> Self, class Env>
      static auto get_item_types(Self&&, Env&&) noexcept -> item_types_t<Self, Env>;
    };
  } // namespace merge_each_

  using merge_each_::merge_each_t;
  inline constexpr merge_each_t merge_each{};
}
