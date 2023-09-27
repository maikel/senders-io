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
      std::atomic<int> error_emplaced_{0};
      stdexec::in_place_stop_source stop_source_{};
      // std::optional<default_stop_callback_t> on_receiver_stopped_{};
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
          int error_emplaced = op_->error_emplaced_.load(std::memory_order_acquire);
          if (error_emplaced == 2) {
            std::visit(
              error_visitor<Receiver>{&op_->receiver_}, static_cast<ErrorsVariant&&>(op_->errors_));
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
        int expected = 0;
        if (op_->error_emplaced_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          op_->errors_.template emplace<decay_t<Error>>(static_cast<Error&&>(error));
          op_->error_emplaced_.store(2, std::memory_order_release);
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

    template <class _Sequence, class _Env>
    using item_completion_signatures_of_t =
      exec::__concat_item_signatures_t< exec::item_types_of_t<_Sequence, _Env>, _Env>;

    template <class _Sequence, class _Env>
    using single_item_value_t = stdexec::__gather_signal<
      stdexec::set_value_t,
      item_completion_signatures_of_t<_Sequence, _Env>,
      stdexec::__msingle_or<void>,
      stdexec::__q<stdexec::__msingle>>;

    template <class Env, class... Senders>
    concept sequence_factory =                                              //
      sizeof...(Senders) == 1 &&                                            //
      stdexec::__single_typed_sender<stdexec::__mfront<Senders...>, Env> && //
      exec::sequence_sender_in<single_item_value_t<stdexec::__mfront<Senders...>, Env>, Env>;

    template <class ItemReceiver, class Receiver, class ErrorsVariant>
    struct dynamic_item_operation_base {
      [[no_unique_address]] ItemReceiver receiver_;
      operation_base<Receiver, ErrorsVariant>* parent_;
    };

    template <class ItemReceiver, class Receiver, class ErrorsVariant>
    struct dynamic_next_receiver {
      using is_receiver = void;

      dynamic_item_operation_base<ItemReceiver, Receiver, ErrorsVariant>* op_;

      stdexec::env_of_t<ItemReceiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->receiver_);
      }

      template <class Sender>
      auto set_next(exec::set_next_t, Sender&& sender) {
        return exec::set_next(op_->parent_->receiver_, static_cast<Sender&&>(sender));
      }

      void set_value(stdexec::set_value_t) && noexcept {
        stdexec::set_value(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
        // stdexec::set_error(static_cast<ItemReceiver&&>(op_->receiver_), static_cast<Error&&>(error));
      }
    };

    template <class Item, class ItemReceiver, class Receiver, class ErrorsVariant>
    struct subsequence_operation
      : dynamic_item_operation_base<ItemReceiver, Receiver, ErrorsVariant> {

      using Subsequence = stdexec::__single_sender_value_t<Item>;

      std::optional<exec::subscribe_result_t<
        Subsequence,
        dynamic_next_receiver<ItemReceiver, Receiver, ErrorsVariant>>>
        op_;

      subsequence_operation(
        ItemReceiver item_receiver,
        operation_base<Receiver, ErrorsVariant>* parent)
        : dynamic_item_operation_base< ItemReceiver, Receiver, ErrorsVariant> {
        static_cast<ItemReceiver&&>(item_receiver), parent
      }

      { }
    };

    template <class Item, class ItemReceiver, class Receiver, class ErrorsVariant>
    struct receive_subsequence {
      using is_receiver = void;

      subsequence_operation<Item, ItemReceiver, Receiver, ErrorsVariant>* op_;

      stdexec::env_of_t<ItemReceiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->receiver_);
      }

      template <class Subsequence>
      void set_value(stdexec::set_value_t, Subsequence&& subsequence) && noexcept {
        try {
          auto& next_op = op_->op_.emplace(stdexec::__conv{[&] {
            return exec::subscribe(
              static_cast<Subsequence&&>(subsequence),
              dynamic_next_receiver<ItemReceiver, Receiver, ErrorsVariant>{op_});
          }});
          stdexec::start(next_op);
        } catch (...) {
          // TODO
          stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
        }
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }
    };

    template <class Item, class ItemReceiver, class Receiver, class ErrorsVariant>
    struct dynamic_item_operation
      : subsequence_operation<Item, ItemReceiver, Receiver, ErrorsVariant> {
      stdexec::
        connect_result_t<Item, receive_subsequence<Item, ItemReceiver, Receiver, ErrorsVariant>>
          receive_op_;

      dynamic_item_operation(
        Item&& item,
        ItemReceiver item_receiver,
        operation_base<Receiver, ErrorsVariant>* parent)
        : subsequence_operation<
          Item,
          ItemReceiver,
          Receiver,
          ErrorsVariant>{static_cast<ItemReceiver&&>(item_receiver), parent}
        , receive_op_{stdexec::connect(
            static_cast<Item&&>(item),
            receive_subsequence<Item, ItemReceiver, Receiver, ErrorsVariant>{this})} {
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(receive_op_);
      }
    };

    template <class Subsequence, class Receiver, class ErrorsVariant>
    struct dynamic_item_sender {
      using is_sender = void;

      using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;

      Subsequence subsequence_;
      operation_base<Receiver, ErrorsVariant>* parent_;

      template <decays_to<dynamic_item_sender> Self, class ItemReceiver>
      static auto connect(Self&& self, stdexec::connect_t, ItemReceiver item_receiver)
        -> dynamic_item_operation<
          copy_cvref_t<Self, Subsequence>,
          ItemReceiver,
          Receiver,
          ErrorsVariant> {
        return {
          static_cast<Self&&>(self).subsequence_,
          static_cast<ItemReceiver&&>(item_receiver),
          self.parent_};
      }
    };

    template <class Receiver, class ErrorsVariant>
    struct dynamic_receiver {
      using is_receiver = void;

      operation_base<Receiver, ErrorsVariant>* parent_;

      auto get_env(stdexec::get_env_t) const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(parent_->receiver_);
      }

      template <class Subsequence>
      auto set_next(exec::set_next_t, Subsequence&& subsequence) //
        noexcept(nothrow_decay_copyable<Subsequence>)            //
        -> dynamic_item_sender<Subsequence, Receiver, ErrorsVariant> {
        return {static_cast<Subsequence&&>(subsequence), parent_};
      }

      void set_value(stdexec::set_value_t) && noexcept {
        int error_emplaced = parent_->error_emplaced_.load(std::memory_order_acquire);
        if (error_emplaced == 2) {
          std::visit(
            error_visitor<Receiver>{&parent_->receiver_},
            static_cast<ErrorsVariant&&>(parent_->errors_));
        } else {
          stdexec::set_value(static_cast<Receiver&&>(parent_->receiver_));
        }
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        int error_emplaced = parent_->error_emplaced_.load(std::memory_order_acquire);
        if (error_emplaced == 2) {
          std::visit(
            error_visitor<Receiver>{&parent_->receiver_},
            static_cast<ErrorsVariant&&>(parent_->errors_));
        } else {
          exec::set_value_unless_stopped(static_cast<Receiver&&>(parent_->receiver_));
        }
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_error(
          static_cast<Receiver&&>(parent_->receiver_), static_cast<Error&&>(error));
      }
    };

    template <class Sender, class Receiver, class ErrorsVariant>
    struct dynamic_operation : operation_base<Receiver, ErrorsVariant> {
      exec::subscribe_result_t<Sender, dynamic_receiver<Receiver, ErrorsVariant>> op_;

      dynamic_operation(Sender&& sndr, Receiver rcvr)
        : operation_base<Receiver, ErrorsVariant>{0, static_cast<Receiver&&>(rcvr)}
        , op_{exec::subscribe(
            static_cast<Sender&&>(sndr),
            dynamic_receiver<Receiver, ErrorsVariant>{this})} {
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(op_);
      }
    };

    template <class... Senders>
    struct sender {
      using is_sender = exec::sequence_tag;


      template <class Self, class Env>
      using value_type_t =
        single_item_value_t<copy_cvref_t<Self, stdexec::__mfront<Senders...>>, Env>;

      std::tuple<Senders...> senders_;

      template <decays_to<sender> Self, class Receiver>
        requires(!sequence_factory<stdexec::env_of_t<Receiver>, copy_cvref_t<Self, Senders>...>)
      static auto subscribe(Self&& self, exec::subscribe_t, Receiver receiver)
        -> operation<Receiver, copy_cvref_t<Self, Senders>...> {
        return std::apply(
          [&]<class... Sndrs>(Sndrs&&... sndrs) {
            return operation<Receiver, Sndrs...>{
              static_cast<Receiver&&>(receiver), static_cast<Sndrs&&>(sndrs)...};
          },
          static_cast<Self&&>(self).senders_);
      }

      template <decays_to<sender> Self, class Receiver>
        requires sequence_factory<stdexec::env_of_t<Receiver>, copy_cvref_t<Self, Senders>...>
      static auto subscribe(Self&& self, exec::subscribe_t, Receiver receiver) //
        -> dynamic_operation<
          copy_cvref_t<Self, stdexec::__mfront<Senders...>>,
          Receiver,
          typename traits<Receiver, copy_cvref_t<Self, Senders>...>::errors_variant> {
        return {std::get<0>(static_cast<Self&&>(self).senders_), static_cast<Receiver&&>(receiver)};
      }

      template <decays_to<sender> Self, class Env>
        requires(!sequence_factory<Env, copy_cvref_t<Self, Senders>...>)
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> stdexec::__concat_completion_signatures_t<
          exec::__to_sequence_completion_signatures<copy_cvref_t<Self, Senders>, Env>...>;

      template <decays_to<sender> Self, class Env>
        requires sequence_factory<Env, copy_cvref_t<Self, Senders>...>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> exec::__to_sequence_completion_signatures<value_type_t<Self, Env>, Env>;

      template <decays_to<sender> Self, class Env>
        requires(!sequence_factory<Env, copy_cvref_t<Self, Senders>...>)
      static auto get_item_types(Self&&, exec::get_item_types_t, Env&&) -> stdexec::__minvoke<
        stdexec::__mconcat<stdexec::__q<exec::item_types>>,
        exec::item_types_of_t<copy_cvref_t<Self, Senders>, Env>...>;

      template <decays_to<sender> Self, class Env>
        requires sequence_factory<Env, copy_cvref_t<Self, Senders>...>
      static auto get_item_types(Self&&, exec::get_item_types_t, Env&&)
        -> exec::item_types_of_t<value_type_t<Self, Env>, Env>;
    };

    struct merge_each_t {
      template <class... Senders>
      auto operator()(Senders&&... senders) const noexcept((nothrow_decay_copyable<Senders> && ...))
        -> sender<decay_t<Senders>...> {
        return {{static_cast<Senders&&>(senders)...}};
      }

      auto operator()() const noexcept -> binder_back<merge_each_t> {
        return {{}, {}, {}};
      }
    };
  } // namespace merge_each_

  using merge_each_::merge_each_t;

  inline constexpr merge_each_t merge_each{};
}