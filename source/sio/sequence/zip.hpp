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
#include "../intrusive_queue.hpp"

#include <optional>

namespace sio {
  namespace zip_ {
    using namespace stdexec;

    template <class ResultTuple>
    struct item_operation_result {
      item_operation_result* next_;
      std::optional<ResultTuple> result_{};
      void (*complete_)(item_operation_result*) noexcept = nullptr;
    };

    template <class... Results>
    using item_operation_queues =
      std::tuple<intrusive_queue<&item_operation_result<Results>::next_>...>;

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
      using on_stop =
        typename stop_token_of_t<env_of_t<Receiver>>::template callback_type<on_stop_requested>;

      [[no_unique_address]] Receiver receiver_;
      [[no_unique_address]] ErrorsVariant errors_{};
      std::mutex stop_mutex_{};
      mutexes_t mutexes_{};
      queues_t item_queues_{};
      std::atomic<int> n_ready_next_items_{};
      in_place_stop_source stop_source_{};
      std::optional<on_stop> stop_callback_{};
      std::atomic<int> n_pending_operations_{std::tuple_size_v<ResultTuple>};

      template <std::size_t Index>
      bool push_back_item_op(
        item_operation_result<std::tuple_element_t<Index, ResultTuple>>* op) noexcept {
        if (!stop_source_.stop_requested()) {
          std::scoped_lock lock(std::get<Index>(mutexes_));
          std::get<Index>(item_queues_).push_back(op);
          return true;
        }
        return false;
      }

      void notify_stop() {
        stop_source_.request_stop();
        auto local_queues = std::apply(
          [this]<same_as<std::mutex>... Mutex>(Mutex&... mutexes) {
            std::scoped_lock lock(mutexes...);
            n_ready_next_items_.store(0, std::memory_order_relaxed);
            return std::apply(
              []<class... Queues>(Queues&... queues) {
                return std::tuple{static_cast<Queues&&>(queues)...};
              },
              item_queues_);
          },
          mutexes_);
        std::apply(
          [](auto&... queues) {
            auto clear_queue = []<class Queue>(Queue& queue) {
              while (!queue.empty()) {
                auto op = queue.pop_front();
                op->complete_(op);
              }
            };
            (clear_queue(queues), ...);
          },
          local_queues);
      }

      template <class Error>
        requires emplaceable<ErrorsVariant, decay_t<Error>, Error>
      void notify_error(Error&& error) {
        {
          std::scoped_lock lock(stop_mutex_);
          if (errors_.index() == 0) {
            errors_.template emplace<decay_t<Error>>(static_cast<Error&&>(error));
          }
        }
        notify_stop();
      }
    };

    template <class Receiver, class ResultTuple, class ErrorsVariant>
    struct zipped_operation_base {
      template <class Tp>
      using to_item_result = item_operation_result<Tp>*;

      using item_ops = __mapply<__transform<__q<to_item_result>, __q<std::tuple>>, ResultTuple>;

      operation_base<Receiver, ResultTuple, ErrorsVariant>* sequence_op_;
      std::optional<item_ops> items_{};

      void complete_all_item_ops() noexcept {
        std::apply(
          [&]<class... Ts>(item_operation_result<Ts>*... item_ops) {
            (item_ops->complete_(item_ops), ...);
          },
          *items_);
      }
    };

    template <class Receiver, class ResultTuple, class ErrorsVariant>
    struct zipped_receiver {
      zipped_operation_base<Receiver, ResultTuple, ErrorsVariant>* op_;

      void set_value(stdexec::set_value_t) && noexcept {
        op_->complete_all_item_ops();
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->sequence_op_->notify_stop();
        op_->complete_all_item_ops();
      }

      auto get_env(stdexec::get_env_t) const noexcept {
        return exec::make_env(
          stdexec::get_env(op_->sequence_op_->receiver_),
          exec::with(get_stop_token, op_->sequence_op_->stop_source_.get_token()));
      }
    };

    template <class ResultTuple>
    using concat_result_types = __mapply<__mconcat<__q<std::tuple>>, ResultTuple>;

    template <class... Args>
    using just_t = decltype(stdexec::just(std::declval<Args>()...));

    template <class ResultTuple>
    using just_sender_t = __mapply<__q<just_t>, concat_result_types<ResultTuple>>;

    template <
      std::size_t Index,
      class ItemReceiver,
      class Receiver,
      class ResultTuple,
      class ErrorsVariant>
    struct item_operation_base
      : item_operation_result<std::tuple_element_t<Index, ResultTuple>>
      , zipped_operation_base<Receiver, ResultTuple, ErrorsVariant> {

      using base_t = item_operation_result<std::tuple_element_t<Index, ResultTuple>>;

      [[no_unique_address]] ItemReceiver item_receiver_;
      std::optional<stdexec::connect_result_t<
        exec::next_sender_of_t<Receiver, just_sender_t<ResultTuple>>,
        zipped_receiver<Receiver, ResultTuple, ErrorsVariant>>>
        zipped_op_{};

      static void complete(base_t* base) noexcept {
        item_operation_base* self = static_cast<item_operation_base*>(base);
        if (self->sequence_op_->stop_source_.stop_requested()) {
          stdexec::set_stopped(static_cast<ItemReceiver&&>(self->item_receiver_));
        } else {
          exec::set_value_unless_stopped(static_cast<ItemReceiver&&>(self->item_receiver_));
        }
      }

      item_operation_base(
        ItemReceiver rcvr,
        operation_base<Receiver, ResultTuple, ErrorsVariant>* sequence_op) noexcept
        : item_operation_result<std::tuple_element_t<Index, ResultTuple>>{{}, {}, &complete}
        , zipped_operation_base<Receiver, ResultTuple, ErrorsVariant>{sequence_op}
        , item_receiver_(static_cast<ItemReceiver&&>(rcvr)) {
      }

      void notify_result_completion() noexcept {
        // Check whether this is the the last item operation to complete such that we can start the zipped operation
        constexpr int n_results = std::tuple_size_v<ResultTuple>;
        operation_base<Receiver, ResultTuple, ErrorsVariant>* sequence_op = this->sequence_op_;
        std::unique_lock lock(std::get<Index>(sequence_op->mutexes_));
        if (std::get<Index>(sequence_op->item_queues_).front() != this) {
          return;
        }
        const int n_ready_next_items = sequence_op->n_ready_next_items_.fetch_add(
          1, std::memory_order_relaxed);
        lock.unlock();
        if (n_ready_next_items == n_results - 1) {
          // 1. Collect all results and assemble one big tuple
          concat_result_types<ResultTuple> result = std::apply(
            [&](auto&... mutexes) {
              std::scoped_lock lock(mutexes...);
              return std::apply(
                [&](auto&... queues) {
                  return std::tuple_cat(std::move(*queues.front()->result_)...);
                },
                sequence_op->item_queues_);
            },
            sequence_op->mutexes_);

          // 2. pop front items from shared queues into a private storage of this op.
          std::apply(
            [&](auto&... mutexes) {
              std::scoped_lock lock(mutexes...);
              this->items_.emplace(std::apply(
                [](auto&... queues) { return std::tuple{queues.pop_front()...}; },
                sequence_op->item_queues_));
              sequence_op->n_ready_next_items_.store(-1, std::memory_order_relaxed);
            },
            sequence_op->mutexes_);

          // 3. Check whether we need to stop
          if (sequence_op->stop_source_.stop_requested()) {
            this->complete_all_item_ops();
            return;
          }

          // 4. count ready items in shared queues
          // An inline completion of the zipped operation could destroy ALL the operations.
          // So we need to check whether there is outstanding work before we start the zipped operation.
          // Since there is no other candidate to complete the ready items, we can safely assume that
          // we will be the one to start the next zipped operation, too.

          bool is_next_completion = false;
          std::apply(
            [&](auto&... mutexes) {
              std::scoped_lock lock(mutexes...);
              const int count = std::apply(
                [](auto&... queues) { return (!queues.empty() + ...); }, sequence_op->item_queues_);
              if (count == n_results) {
                sequence_op->n_ready_next_items_.store(count - 1, std::memory_order_relaxed);
                is_next_completion = true;
              } else {
                sequence_op->n_ready_next_items_.store(count, std::memory_order_relaxed);
              }
            },
            sequence_op->mutexes_);

          // 5. Start the zipped operation.
          try {
            auto& op = zipped_op_.emplace(stdexec::__conv{[&] {
              return stdexec::connect(
                exec::set_next(
                  sequence_op->receiver_,
                  std::apply(
                    []<class... Args>(Args&&... args) {
                      return stdexec::just(static_cast<Args&&>(args)...);
                    },
                    static_cast<concat_result_types<ResultTuple>&&>(result))),
                zipped_receiver<Receiver, ResultTuple, ErrorsVariant>{this});
            }});
            stdexec::start(op);
          } catch (...) {
            sequence_op->notify_error(std::current_exception());
            this->complete_all_item_ops();
            return;
          }

          // 6. If all next items are ready, then start the next zipped operation
          if (is_next_completion) {
            static_cast<item_operation_base*>(std::get<Index>(sequence_op->item_queues_).front())
              ->notify_result_completion();
          }
        }
      }
    };

    template <
      std::size_t Index,
      class ItemReceiver,
      class Receiver,
      class ResultTuple,
      class ErrorsVariant>
    struct item_receiver {
      item_operation_base<Index, ItemReceiver, Receiver, ResultTuple, ErrorsVariant>* op_;

      template <class... Ts>
      void set_value(stdexec::set_value_t, Ts&&... args) && noexcept {
        try {
          op_->result_.emplace(static_cast<Ts&&>(args)...);
        } catch (...) {
          op_->sequence_op_->notify_error(std::current_exception());
          stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
        }
        if (!op_->sequence_op_->template push_back_item_op<Index>(op_)) {
          stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
        } else {
          op_->notify_result_completion();
        }
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->sequence_op_->notify_stop();
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        op_->sequence_op_->notify_error(static_cast<Error&&>(error));
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
      }

      env_of_t<ItemReceiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->item_receiver_);
      }
    };

    template <
      std::size_t Index,
      class Item,
      class ItemReceiver,
      class Receiver,
      class ResultTuple,
      class ErrorsVariant>
    struct item_operation
      : item_operation_base<Index, ItemReceiver, Receiver, ResultTuple, ErrorsVariant> {
      using item_receiver_t =
        item_receiver<Index, ItemReceiver, Receiver, ResultTuple, ErrorsVariant>;
      stdexec::connect_result_t<Item, item_receiver_t> item_op_;

      item_operation(
        Item&& item,
        ItemReceiver&& item_receiver,
        operation_base<Receiver, ResultTuple, ErrorsVariant>* sequence_op)
        : item_operation_base<
          Index,
          ItemReceiver,
          Receiver,
          ResultTuple,
          ErrorsVariant>{static_cast<ItemReceiver&&>(item_receiver), sequence_op}
        , item_op_{stdexec::connect(static_cast<Item&&>(item), item_receiver_t{this})} {
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(item_op_);
      }
    };

    template <std::size_t Index, class Item, class Receiver, class ResultTuple, class ErrorsVariant>
    struct item_sender {
      [[no_unique_address]] Item item_;
      operation_base<Receiver, ResultTuple, ErrorsVariant>* op_;

      using completion_signatures = stdexec::completion_signatures<set_value_t(), set_stopped_t()>;

      template <same_as<item_sender> Self, class ItemReceiver>
      static auto connect(Self&& self, stdexec::connect_t, ItemReceiver item_rcvr)
        -> item_operation<
          Index,
          copy_cvref_t<Self, Item>,
          ItemReceiver,
          Receiver,
          ResultTuple,
          ErrorsVariant> {
        return {static_cast<Self&&>(self).item_, static_cast<ItemReceiver&&>(item_rcvr), self.op_};
      }
    };

    template <std::size_t Index, class Receiver, class ResultTuple, class ErrorsVariant>
    struct receiver {
      operation_base<Receiver, ResultTuple, ErrorsVariant>* op_;

      template <class Item>
      auto set_next(exec::set_next_t, Item&& item) noexcept(nothrow_decay_copyable<Item>)
        -> item_sender<Index, decay_t<Item>, Receiver, ResultTuple, ErrorsVariant> {
        return {static_cast<Item&&>(item), op_};
      }

      void set_value(stdexec::set_value_t) && noexcept {
        int n_ops = op_->n_pending_operations_.fetch_sub(1, std::memory_order_relaxed);
        if (n_ops > 1) {
          op_->notify_stop();
          return;
        }
        auto token = stdexec::get_stop_token(stdexec::get_env(op_->receiver_));
        if (token.stop_requested()) {
          stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
        } else if (op_->errors_.index()) {
          std::visit(
            [&]<class Error>(Error&& error) {
              if constexpr (__not_decays_to<Error, std::monostate>) {
                stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), (Error&&) error);
              }
            },
            static_cast<ErrorsVariant&&>(op_->errors_));
        } else {
          stdexec::set_value(static_cast<Receiver&&>(op_->receiver_));
        }
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        int n_ops = op_->n_pending_operations_.fetch_sub(1, std::memory_order_relaxed);
        if (n_ops > 1) {
          op_->notify_stop();
          return;
        }
        if (op_->stop_source_.stop_requested()) {
          exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->receiver_));
        } else {
          stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
        }
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        int n_ops = op_->n_pending_operations_.fetch_sub(1, std::memory_order_relaxed);
        if (n_ops > 1) {
          op_->notify_error(static_cast<Error&&>(error));
          return;
        }
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), static_cast<Error&&>(error));
      }

      auto get_env(stdexec::get_env_t) const noexcept {
        return exec::make_env(
          stdexec::get_env(op_->receiver_),
          exec::with(get_stop_token, op_->stop_source_.get_token()));
      }
    };


    template <class Tp>
    using decay_rvalue_ref = decay_t<Tp>&&;

    template <class Sender, class Env>
    concept max1_sender =
      sender_in<Sender, Env>
      && __valid<__value_types_of_t, Sender, Env, __mconst<int>, __msingle_or<void>>;

    template <class Env, class Sender>
    using single_values_of_t = //
      __value_types_of_t<
        Sender,
        Env,
        __transform<__q<decay_rvalue_ref>, __q<__types>>,
        __q<__msingle>>;

    template <class Env>
    using env_t = decltype(exec::make_env(
      __declval<Env>(),
      exec::with(get_stop_token, __declval<in_place_stop_token>())));

    template <class Env, class Sender>
    using values_tuple_t = //
      __value_types_of_t<Sender, env_t<Env>, __q<__decayed_tuple>, __q<__msingle>>;

    template <class Env, class... Senders>
    using set_values_sig_t = //
      completion_signatures<
        __minvoke< __mconcat<__qf<set_value_t>>, single_values_of_t<Env, Senders>...>>;

    template <class Env, max1_sender<Env>... Senders>
    using completions_t = //
      __concat_completion_signatures_t<
        completion_signatures<set_stopped_t(), set_error_t(std::exception_ptr&&)>,
        __minvoke<
          __with_default<__mbind_front_q<set_values_sig_t, Env>, completion_signatures<>>,
          Senders...>,
        __try_make_completion_signatures<
          Senders,
          Env,
          completion_signatures<>,
          __mconst<completion_signatures<>>,
          __mcompose<__q<completion_signatures>, __qf<set_error_t>, __q<decay_rvalue_ref>>>...>;

    template <class Receiver, class... Senders>
    struct traits {
      using result_tuple = __minvoke<
        __transform<__mbind_front_q<values_tuple_t, env_of_t<Receiver>>, __q<std::tuple>>,
        Senders...>;

      using errors_variant = //
        __minvoke<
          __mconcat<__transform<__q<decay_t>, __nullable_variant_t>>,
          __types<std::exception_ptr>,
          error_types_of_t<Senders, env_t<env_of_t<Receiver>>, __types>... >;

      using operation_base = zip_::operation_base<Receiver, result_tuple, errors_variant>;

      template <std::size_t Is>
      using receiver = zip_::receiver<Is, Receiver, result_tuple, errors_variant>;

      template <class Sender, class Index>
      using op_state = exec::subscribe_result_t<Sender, receiver<__v<Index>>>;

      template <class Tuple = __q<std::tuple>>
      using op_states_tuple = //
        __minvoke<
          __mzip_with2<__q<op_state>, Tuple>,
          __types<Senders...>,
          __mindex_sequence_for<Senders...>>;
    };

    template <class Receiver, class... Senders>
    struct operation
      : operation_base<
          Receiver,
          typename traits<Receiver, Senders...>::result_tuple,
          typename traits<Receiver, Senders...>::errors_variant> {
      using base_type = operation_base<
        Receiver,
        typename traits<Receiver, Senders...>::result_tuple,
        typename traits<Receiver, Senders...>::errors_variant>;

      typename traits<Receiver, Senders...>::template op_states_tuple<> op_states_;

      template <class SenderTuple, std::size_t... Is>
      operation(Receiver rcvr, SenderTuple&& sndr, std::index_sequence<Is...>)
        : base_type{{}, static_cast<Receiver&&>(rcvr)}
        , op_states_(stdexec::__conv{[&] {
          return exec::subscribe(
            std::get<Is>(static_cast<SenderTuple&&>(sndr)),
            typename traits<Receiver, Senders...>::template receiver<Is>{this});
        }}...) {
      }

      void start(stdexec::start_t) noexcept {
        std::apply([](auto&... ops) { (stdexec::start(ops), ...); }, op_states_);
      }
    };

    template <class Indices, class... Senders>
    struct sender;

    template <std::size_t... Is, class... Senders>
    struct sender<std::index_sequence<Is...>, Senders...> {
      using is_sender = exec::sequence_tag;

      std::tuple<Senders...> senders_;

      template <decays_to<sender> Self, class Receiver>
      static auto subscribe(Self&& self, exec::subscribe_t, Receiver rcvr) //
        noexcept(nothrow_constructible_from<
                 operation<Receiver, copy_cvref_t<Self, Senders>...>,
                 Receiver,
                 copy_cvref_t<Self, std::tuple<Senders...>>,
                 std::index_sequence<Is...>>)
          -> operation<Receiver, copy_cvref_t<Self, Senders>...> {
        return {
          static_cast<Receiver&&>(rcvr),
          static_cast<Self&&>(self).senders_,
          std::index_sequence<Is...>{}};
      }

      template <decays_to<sender> Self, class Env>
      static auto
        get_completion_signatures(Self&& self, stdexec::get_completion_signatures_t, Env&& env)
          -> completions_t<Env, Senders...>;
    };

    struct zip_t {
      template <stdexec::sender... Senders>
      auto operator()(Senders&&... senders) const noexcept((nothrow_decay_copyable<Senders> && ...))
        -> sender<std::index_sequence_for<Senders...>, decay_t<Senders>...> {
        return {{static_cast<Senders&&>(senders)...}};
      }
    };
  }

  using zip_::zip_t;
  inline constexpr zip_t zip{};
}