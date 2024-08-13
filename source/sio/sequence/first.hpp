/*
 * Copyright (c) 2024 Maikel Nadolski
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

#include <stdexec/execution.hpp>
#include <exec/sequence_senders.hpp>

#include "../concepts.hpp"
#include "./sequence_concepts.hpp"

namespace sio {
  namespace first_ {
    using namespace stdexec;

    template <class ResultVariant, bool IsLockStep>
    struct result_type {
      stdexec::__manual_lifetime<ResultVariant> result_{};
      std::atomic<int> emplaced_{0};

      template <class... Args>
      void emplace(Args&&... args) noexcept {
        int expected = 0;
        if (emplaced_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          result_.__construct(
            std::in_place_type<__decayed_std_tuple<Args...>>, static_cast<Args&&>(args)...);
          emplaced_.store(2, std::memory_order_release);
        }
      }

      template <class Receiver>
      void visit_result(Receiver&& rcvr) noexcept {
        int is_emplaced = emplaced_.load(std::memory_order_acquire);
        if (!is_emplaced) {
          stdexec::set_stopped(static_cast<Receiver&&>(rcvr));
        }
        std::visit(
          [&]<class Tuple>(Tuple&& tuple) noexcept {
            if constexpr (__not_decays_to<Tuple, std::monostate>) {
              std::apply(
                [&]<__completion_tag Tag, class... Args>(Tag completion, Args&&... args) noexcept {
                  completion(static_cast<Receiver&&>(rcvr), static_cast<Args&&>(args)...);
                },
                static_cast<Tuple&&>(tuple));
            }
          },
          static_cast<ResultVariant&&>(result_.__get()));
      }
    };

    template <class ResultVariant>
    struct result_type<ResultVariant, true> {
      stdexec::__manual_lifetime<ResultVariant> result_{};

      template <class... Args>
      void emplace(Args&&... args) noexcept {
        if (result_.index() == 0) {
          result_.__construct(
            std::in_place_type<__decayed_std_tuple<Args...>>, static_cast<Args&&>(args)...);
        }
      }

      template <class Receiver>
      void visit_result(Receiver&& rcvr) noexcept {
        std::visit(
          [&]<class Tuple>(Tuple&& tuple) noexcept {
            if constexpr (__not_decays_to<Tuple, std::monostate>) {
              std::apply(
                [&]<class Tag, class... Args>(Tag completion, Args&&... args) noexcept {
                  static_assert(__completion_tag<Tag>);
                  completion(static_cast<Receiver&&>(rcvr), static_cast<Args&&>(args)...);
                },
                static_cast<Tuple&&>(tuple));
            } else {
              stdexec::set_stopped(static_cast<Receiver&&>(rcvr));
            }
          },
          static_cast<ResultVariant&&>(result_.__get()));
      }
    };

    template <class ItemReceiver, class ResultVariant, bool IsLockStep>
    struct item_operation_base {
      [[no_unique_address]] ItemReceiver receiver_;
      result_type<ResultVariant, IsLockStep>* result_;
    };

    template <class ItemReceiver, class ResultVariant, bool IsLockStep>
    struct item_receiver {
      using receiver_concept = stdexec::receiver_t;
      item_operation_base<ItemReceiver, ResultVariant, IsLockStep>* op_;

      template <class... Args>
      void set_value(Args&&... args) && noexcept {
        op_->result_->emplace(set_value_t{}, static_cast<Args&&>(args)...);
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      void set_stopped() && noexcept {
        op_->result_->emplace(set_stopped_t{});
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      template <class Error>
        requires emplaceable<
          ResultVariant,
          __decayed_std_tuple<set_error_t, Error>,
          set_error_t,
          Error>
      void set_error(Error&& error) && noexcept {
        op_->result_->emplace(set_error_t{}, static_cast<Error&&>(error));
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      auto get_env() const noexcept -> env_of_t<ItemReceiver> {
        return stdexec::get_env(op_->receiver_);
      }
    };

    template <class Sender, class ItemReceiver, class ResultVariant, bool IsLockStep>
    struct item_operation : item_operation_base<ItemReceiver, ResultVariant, IsLockStep> {
      using base_type = item_operation_base<ItemReceiver, ResultVariant, IsLockStep>;
      using item_receiver_t = item_receiver<ItemReceiver, ResultVariant, IsLockStep>;

      stdexec::connect_result_t<Sender, item_receiver_t> op_;

      item_operation(result_type<ResultVariant, IsLockStep>* parent, Sender&& sndr, ItemReceiver rcvr) noexcept(
        __nothrow_decay_copyable<ItemReceiver> && __nothrow_connectable<Sender, item_receiver_t>)
        : base_type{static_cast<ItemReceiver&&>(rcvr), parent}
        , op_{stdexec::connect(static_cast<Sender&&>(sndr), item_receiver_t{this})} {
      }

      void start() noexcept {
        stdexec::start(op_);
      }
    };

    template <class Sender, class ResultVariant, bool IsLockStep>
    struct item_sender {
      using sender_concept = stdexec::sender_t;
      using completion_signatures = stdexec::completion_signatures<set_stopped_t()>;

      template <class Receiver>
      using item_operation_t = item_operation<Sender, Receiver, ResultVariant, IsLockStep>;

      template <class Receiver>
      using item_receiver_t = item_receiver<Receiver, ResultVariant, IsLockStep>;

      Sender sender_;
      result_type<ResultVariant, IsLockStep>* parent_;

      template <stdexec::receiver Receiver>
        requires sender_to<Sender, item_receiver_t<Receiver>>
      auto connect(Receiver rcvr) -> item_operation_t<Receiver> {
        return {parent_, static_cast<Sender&&>(sender_), static_cast<Receiver&&>(rcvr)};
      }
    };

    template <class ReceiverId, class ResultVariant, bool IsLockStep>
    struct operation_base : result_type<ResultVariant, IsLockStep> {
      using Receiver = stdexec::__t<ReceiverId>;
      [[no_unique_address]] Receiver receiver_;
    };

    template <class ReceiverId, class ResultVariant, bool IsLockStep>
    struct receiver {
      using __t = receiver;
      using __id = receiver;
      using receiver_concept = stdexec::receiver_t;
      using Receiver = stdexec::__t<ReceiverId>;

      operation_base<ReceiverId, ResultVariant, IsLockStep>* op_;

      template <class Item>
      friend auto tag_invoke(exec::set_next_t, receiver& self, Item&& item) noexcept(
        nothrow_decay_copyable<Item>) -> item_sender<decay_t<Item>, ResultVariant, IsLockStep> {
        return {static_cast<Item&&>(item), self.op_};
      }

      void set_value() && noexcept {
        op_->visit_result(static_cast<Receiver&&>(op_->receiver_));
      }

      void set_stopped() && noexcept {
        stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), static_cast<Error&&>(error));
      }

      auto get_env() const noexcept -> env_of_t<Receiver> {
        return stdexec::get_env(op_->receiver_);
      }
    };

    template <class Sender, class Env>
    struct traits {
      using seq_env_t = exec::sequence_env_of_t<Sender>;
      using parallelism_t = decltype(exec::parallelism(__declval<seq_env_t>()));
      static constexpr bool IsLockStep = same_as<parallelism_t, exec::lock_step_t>;

      using result_variant_t = stdexec::__for_each_completion_signature<
        exec::item_completion_signatures_of_t<Sender, Env>,
        __decayed_std_tuple,
        __std_variant>;
    };


    template <class Sender, class ReceiverId>
    using base_type = operation_base<
      ReceiverId,
      typename traits<Sender, env_of_t<stdexec::__t<ReceiverId>>>::result_variant_t,
      traits<Sender, env_of_t<stdexec::__t<ReceiverId>>>::IsLockStep>;

    template <class Sender, class ReceiverId>
    struct operation : base_type<Sender, ReceiverId> {
      using __t = operation;
      using __id = operation;
      using Receiver = stdexec::__t<ReceiverId>;
      using env_t = env_of_t<stdexec::__t<ReceiverId>>;
      using receiver_t = first_::receiver<
        ReceiverId,
        typename traits<Sender, env_t >::result_variant_t,
        traits<Sender, env_t>::IsLockStep>;

      exec::subscribe_result_t<Sender, receiver_t> op_;

      operation(Sender&& sndr, Receiver rcvr) //
        noexcept(
          nothrow_decay_copyable<Receiver> //
          && exec::nothrow_subscribeable<Sender, receiver_t>)
        : base_type<Sender, ReceiverId>{{}, static_cast<Receiver&&>(rcvr)}
        , op_{exec::subscribe(static_cast<Sender&&>(sndr), receiver_t{this})} {
      }

      void start() noexcept {
        stdexec::start(op_);
      }
    };

    template <class Receiver>
    struct connect_fn {
      Receiver& rcvr;

      using ReceiverId = __id<Receiver>;
      using Env = env_of_t<Receiver>;

      template <class Child>
      using result_variant_t = typename traits<Child, Env>::result_variant_t;

      template <class Child>
      using receiver_t =
        first_::receiver<ReceiverId, result_variant_t<Child>, traits<Child, Env>::IsLockStep>;

      template <class Child>
      using operation_t = stdexec::__t<operation<Child, ReceiverId>>;

      template <class Child>
        requires exec::sequence_sender_to<Child, receiver_t<Child>>
      auto operator()(__ignore, __ignore, Child&& child) noexcept(
        __nothrow_constructible_from<operation_t<Child>, Child, Receiver>) -> operation_t<Child> {
        return {static_cast<Child&&>(child), static_cast<Receiver&&>(rcvr)};
      }
    };

    struct first_t {
      template <exec::sequence_sender<stdexec::empty_env> Sender>
      auto operator()(Sender&& seq) const -> stdexec::__well_formed_sender auto {
        auto domain = stdexec::__get_early_domain(static_cast<Sender&&>(seq));
        return stdexec::transform_sender(
          domain, stdexec::__make_sexpr<first_t>(__{}, static_cast<Sender&&>(seq)));
      }

      template <stdexec::sender Sender>
      auto operator()(Sender&& sndr) const noexcept {
        return static_cast<Sender&&>(sndr);
      }

      constexpr auto operator()() const noexcept -> binder_back<first_t> {
        return {{}, {}, {}};
      }
    };

    struct first_impl : stdexec::__sexpr_defaults {
      template <class Sequence, class... Env>
      using sequence_completion_signatures_of_t = transform_completion_signatures<
        stdexec::__completion_signatures_of_t<Sequence, Env...>,
        exec::item_completion_signatures_of_t<Sequence, Env...>,
        stdexec::__mconst<stdexec::completion_signatures<>>::__f >;

      static constexpr auto get_completion_signatures =
        []<class Self, class... Env>(Self&&, Env&&...) noexcept
        -> sequence_completion_signatures_of_t<stdexec::__child_of<Self>, Env...> {
        static_assert(sender_expr_for<Self, first_t>);
        return {};
      };

      static constexpr auto connect =
        []<class Sender, stdexec::receiver Receiver>(Sender&& sndr, Receiver rcvr) noexcept
        -> stdexec::__call_result_t<stdexec::__sexpr_apply_t, Sender, connect_fn<Receiver>> {
        static_assert(sender_expr_for<Sender, first_t>);
        return stdexec::__sexpr_apply(static_cast<Sender&&>(sndr), connect_fn<Receiver>{rcvr});
      };
    };

  } // namespace fist_

  using first_::first_t;
  inline constexpr first_t first{};
} // namespace sio

namespace stdexec {
  template <>
  struct __sexpr_impl<sio::first_::first_t> : sio::first_::first_impl { };
}
