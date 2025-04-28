/*
 * Copyright (c) 2024 Maikel Nadolski
 * Copyright (c) 2024 Emmett Zhang
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
#include "../intrusive_queue.hpp"

#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <stdexec/functional.hpp>
#include <exec/__detail/__basic_sequence.hpp>

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
      inplace_stop_source& stop_source_;

      void operator()() const noexcept {
        stop_source_.request_stop();
      }
    };

    template <class Receiver, class ResultTuple, class ErrorsVariant>
    struct operation_base : __immovable {
      using mutexes_t = __mapply<__mtransform<__mconst<std::mutex>, __q<std::tuple>>, ResultTuple>;
      using queues_t = __mapply<__q<item_operation_queues>, ResultTuple>;
      using on_stop =
        typename stop_token_of_t<env_of_t<Receiver>>::template callback_type<on_stop_requested>;

      [[no_unique_address]] Receiver receiver_;
      [[no_unique_address]] ErrorsVariant errors_{};
      std::mutex stop_mutex_{};
      mutexes_t mutexes_{};
      queues_t item_queues_{};
      std::atomic<int> n_ready_next_items_{};
      inplace_stop_source stop_source_{};
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

      using item_ops = __mapply<__mtransform<__q<to_item_result>, __q<std::tuple>>, ResultTuple>;

      operation_base<Receiver, ResultTuple, ErrorsVariant>* sequence_op_;
      std::optional<item_ops> item_ops_{};

      void complete_all_item_ops() noexcept {
        std::apply(
          [&]<class... Ts>(item_operation_result<Ts>*... item_ops) {
            (item_ops->complete_(item_ops), ...);
          },
          *item_ops_);
      }
    };


    template <class Env>
    using env_with_stop_token = decltype(exec::make_env(
      __declval<Env>(),
      exec::with(get_stop_token, __declval<inplace_stop_token>())));

    template <class Receiver, class ResultTuple, class ErrorsVariant>
    struct zipped_receiver {
      using receiver_concept = stdexec::receiver_t;
      zipped_operation_base<Receiver, ResultTuple, ErrorsVariant>* op_;

      void set_value() && noexcept {
        op_->complete_all_item_ops();
      }

      void set_stopped() && noexcept {
        op_->sequence_op_->notify_stop();
        op_->complete_all_item_ops();
      }

      auto get_env() const noexcept -> env_with_stop_token<env_of_t<Receiver>> {
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

      using item_base_t = item_operation_result<std::tuple_element_t<Index, ResultTuple>>;
      using zipped_base_t = zipped_operation_base<Receiver, ResultTuple, ErrorsVariant>;

      [[no_unique_address]] ItemReceiver item_receiver_;
      std::optional<stdexec::connect_result_t<
        exec::next_sender_of_t<Receiver, just_sender_t<ResultTuple>>,
        zipped_receiver<Receiver, ResultTuple, ErrorsVariant>>>
        zipped_op_{};

      static void complete(item_base_t* base) noexcept {
        auto* self = static_cast<item_operation_base*>(base);
        if (self->sequence_op_->stop_source_.stop_requested()) {
          stdexec::set_stopped(static_cast<ItemReceiver&&>(self->item_receiver_));
        } else {
          exec::set_value_unless_stopped(static_cast<ItemReceiver&&>(self->item_receiver_));
        }
      }

      item_operation_base(
        ItemReceiver rcvr,
        operation_base<Receiver, ResultTuple, ErrorsVariant>* sequence_op) noexcept
        : item_base_t{{}, {}, &complete}
        , zipped_base_t{sequence_op}
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
              this->item_ops_.emplace(std::apply(
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
            auto& op = zipped_op_.emplace(stdexec::__emplace_from{[&] {
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
      using receiver_concept = stdexec::receiver_t;

      item_operation_base<Index, ItemReceiver, Receiver, ResultTuple, ErrorsVariant>* op_;

      template <class... Ts>
      void set_value(Ts&&... args) && noexcept {
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

      void set_stopped() && noexcept {
        op_->sequence_op_->notify_stop();
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        op_->sequence_op_->notify_error(static_cast<Error&&>(error));
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
      }

      auto get_env() const noexcept -> env_of_t<ItemReceiver> {
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
      using base_type =
        item_operation_base<Index, ItemReceiver, Receiver, ResultTuple, ErrorsVariant>;
      using item_receiver_t =
        item_receiver<Index, ItemReceiver, Receiver, ResultTuple, ErrorsVariant>;

      stdexec::connect_result_t<Item, item_receiver_t> item_op_;

      item_operation(
        Item&& item,
        ItemReceiver&& item_receiver,
        operation_base<Receiver, ResultTuple, ErrorsVariant>* sequence_op)
        : base_type{static_cast<ItemReceiver&&>(item_receiver), sequence_op}
        , item_op_{stdexec::connect(static_cast<Item&&>(item), item_receiver_t{this})} {
      }

      void start() noexcept {
        stdexec::start(item_op_);
      }
    };

    template <std::size_t Index, class Item, class Receiver, class ResultTuple, class ErrorsVariant>
    struct item_sender {
      using sender_concept = stdexec::sender_t;

      [[no_unique_address]] Item item_;
      operation_base<Receiver, ResultTuple, ErrorsVariant>* op_;

      using completion_signatures = stdexec::completion_signatures<set_value_t(), set_stopped_t()>;

      template <class ItemReceiver>
      auto connect(ItemReceiver item_rcvr)
        -> item_operation< Index, Item, ItemReceiver, Receiver, ResultTuple, ErrorsVariant> {
        return {static_cast<Item&&>(item_), static_cast<ItemReceiver&&>(item_rcvr), op_};
      }
    };

    template <std::size_t Index, class Receiver, class ResultTuple, class ErrorsVariant>
    struct receiver {
      using receiver_concept = stdexec::receiver_t;
      operation_base<Receiver, ResultTuple, ErrorsVariant>* op_;

      template <class Item>
      friend auto tag_invoke(exec::set_next_t, receiver& self, Item&& item) noexcept(
        nothrow_decay_copyable<Item>)
        -> item_sender<Index, decay_t<Item>, Receiver, ResultTuple, ErrorsVariant> {
        return {static_cast<Item&&>(item), self.op_};
      }

      void set_value() && noexcept {
        int n_ops = op_->n_pending_operations_.fetch_sub(1, std::memory_order_relaxed);
        if (n_ops > 1) {
          op_->notify_stop();
          return;
        }
        op_->stop_callback_.reset();
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

      void set_stopped() && noexcept {
        int n_ops = op_->n_pending_operations_.fetch_sub(1, std::memory_order_relaxed);
        if (n_ops > 1) {
          op_->notify_stop();
          return;
        }
        op_->stop_callback_.reset();
        if (op_->stop_source_.stop_requested()) {
          exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->receiver_));
        } else {
          stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
        }
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        int n_ops = op_->n_pending_operations_.fetch_sub(1, std::memory_order_relaxed);
        if (n_ops > 1) {
          op_->notify_error(static_cast<Error&&>(error));
          return;
        }
        op_->stop_callback_.reset();
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), static_cast<Error&&>(error));
      }

      auto get_env() const noexcept -> env_with_stop_token<env_of_t<Receiver>> {
        return exec::make_env(
          stdexec::get_env(op_->receiver_),
          exec::with(get_stop_token, op_->stop_source_.get_token()));
      }
    };


    template <class Env, class Sender>
    using values_tuple_t = //
      __value_types_of_t<Sender, env_with_stop_token<Env>, __q<std::tuple>, __q<__msingle>>;

    template <class Env, class... Senders>
    using all_items_t = __minvoke< __mconcat<>, exec::item_types_of_t<Senders, Env>...>;

    template <class Env, class... Senders>
    using result_tuple_t = __mapply<
      __mtransform<__mbind_front_q<values_tuple_t, Env>, __q<std::tuple>>,
      all_items_t<Env, Senders...>>;

    template <class Receiver, class... Senders>
    struct traits {
      using env_of_receiver = env_of_t<Receiver>;
      using result_tuple = result_tuple_t<env_of_receiver, Senders...>;

      using error_types_items_t = __mapply<
        __mconcat<>,
        __mapply<
          __mtransform<__mbind_back_q<
            stdexec::__error_types_of_t,
            env_with_stop_token<env_of_receiver>,
            __q<__types>>>,
          all_items_t<env_of_receiver, Senders...>>>;


      using errors_variant = __minvoke<
        __mconcat<__mtransform<__q<decay_t>, __q<__nullable_std_variant>>>,
        __types<std::exception_ptr>,
        error_types_items_t,
        stdexec::error_types_of_t<Senders, env_with_stop_token<env_of_receiver>, __types>...>;


      using operation_base = zip_::operation_base<Receiver, result_tuple, errors_variant>;

      template <std::size_t Is>
      using receiver = zip_::receiver<Is, Receiver, result_tuple, errors_variant>;

      template <class T>
      struct indexes;

      template <class Tp, Tp... Index>
      struct indexes<std::integer_sequence<Tp, Index...>> {
        using value = __types<__mconstant<Index>...>;
      };

      template <class Sender, class Index>
      using op_state = exec::subscribe_result_t<Sender, receiver<__v<Index>>>;

      template <class Tuple = __q<std::tuple>>
      using op_states_tuple = //
        __minvoke<
          __mzip_with2<__q<op_state>, Tuple>,
          __types<Senders...>, //
          typename indexes< std::index_sequence_for< Senders... >>::value>;
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
        , op_states_(stdexec::__emplace_from{[&] {
          return exec::subscribe(
            std::get<Is>(static_cast<SenderTuple&&>(sndr)),
            typename traits<Receiver, Senders...>::template receiver<Is>{this});
        }}...) {
      }

      void start() noexcept {
        this->stop_callback_.emplace(
          stdexec::get_stop_token(stdexec::get_env(this->receiver_)),
          on_stop_requested{this->stop_source_});
        std::apply([](auto&... ops) { (stdexec::start(ops), ...); }, op_states_);
      }
    };

    template <class Receiver>
    struct subscribe_fn {
      Receiver& rcvr_;

      template <class... Children>
      auto operator()(stdexec::__ignore, stdexec::__ignore, Children&&... children) noexcept
        -> operation<Receiver, Children&&...> {
        return {
          static_cast<Receiver&&>(rcvr_),
          std::tuple<Children...>{static_cast<Children&&>(children)...},
          std::index_sequence_for<Children...>()};
      }
    };

    struct zip_t {
      template <stdexec::sender... Senders>
        requires stdexec::__has_common_domain<Senders...>
      auto operator()(Senders&&... senders) const
        noexcept((nothrow_decay_copyable<Senders> && ...)) -> stdexec::__well_formed_sender auto {
        auto domain = stdexec::__domain::__common_domain_t<Senders...>();
        return stdexec::transform_sender(
          domain,
          exec::make_sequence_expr<zip_t>(stdexec::__{}, static_cast<Senders&&>(senders)...));
      }

      template <stdexec::sender_expr_for<zip_t> Self, class Receiver>
      static auto subscribe(Self&& self, Receiver rcvr) noexcept(
        stdexec::__nothrow_invocable<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>>)
        -> stdexec::__call_result_t<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>> {
        return stdexec::__sexpr_apply(static_cast<Self&&>(self), subscribe_fn< Receiver>{rcvr});
      }

      template <class Env, class... Senders>
      using completions_t = //
        __concat_completion_signatures<
          completion_signatures<set_value_t(), set_stopped_t(), set_error_t(std::exception_ptr)>,
          __try_make_completion_signatures<
            Senders,
            Env,
            completion_signatures<>,
            __mconst<completion_signatures<>>,
            __mcompose<__q<completion_signatures>, __qf<set_error_t>, __q<decay_t>>>...>;


      template <stdexec::sender_expr_for<zip_t> Self, class Env>
      static auto get_completion_signatures(Self&& self, Env&& env) noexcept
        -> stdexec::__children_of<Self, __mbind_front_q<completions_t, Env>>;


      template <stdexec::sender_expr_for<zip_t> Self, class Env>
      static auto get_item_types(Self&& self, Env&& env) noexcept
        -> exec::item_types<
          just_sender_t<stdexec::__children_of<Self, __mbind_front_q<result_tuple_t, Env>>>>;
    };
  } // namespace zip_

  using zip_::zip_t;
  inline constexpr zip_t zip{};
}
