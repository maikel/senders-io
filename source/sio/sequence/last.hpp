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

#include "../concepts.hpp"
#include "./sequence_concepts.hpp"

namespace sio {
  namespace last_ {
    using namespace stdexec;

    template <class ResultVariant, bool IsLockStep>
    struct result_type {
      ResultVariant result_{};
      std::mutex mutex_{};

      template <class... Args>
      void emplace(Args&&... args) noexcept {
        std::scoped_lock lock{mutex_};
        result_.template emplace<decayed_tuple<Args...>>(static_cast<Args&&>(args)...);
      }

      template <class Receiver>
      void visit_result(Receiver&& rcvr) noexcept {
        std::scoped_lock lock{mutex_};
        std::visit(
          [&]<class Tuple>(Tuple&& tuple) noexcept {
            if constexpr (__not_decays_to<Tuple, std::monostate>) {
              std::apply(
                [&]<__completion_tag Tag, class... Args>(Tag completion, Args&&... args) noexcept {
                  completion(static_cast<Receiver&&>(rcvr), static_cast<Args&&>(args)...);
                },
                static_cast<Tuple&&>(tuple));
            } else {
              stdexec::set_stopped(static_cast<Receiver&&>(rcvr));
            }
          },
          static_cast<ResultVariant&&>(result_));
      }
    };

    template <class ResultVariant>
    struct result_type<ResultVariant, true> {
      ResultVariant result_{};

      template <class... Args>
      void emplace(Args&&... args) noexcept {
        result_.template emplace<decayed_tuple<Args...>>(static_cast<Args&&>(args)...);
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
          static_cast<ResultVariant&&>(result_));
      }
    };

    template <class ItemReceiver, class ResultVariant, bool IsLockStep>
    struct item_operation_base {
      [[no_unique_address]] ItemReceiver receiver_;
      result_type<ResultVariant, IsLockStep>* result_;
    };

    template <class ItemReceiver, class ResultVariant, bool IsLockStep>
    struct item_receiver {
      using is_receiver = void;
      item_operation_base<ItemReceiver, ResultVariant, IsLockStep>* op_;

      template <class... Args>
      void set_value(set_value_t, Args&&... args) && noexcept {
        op_->result_->emplace(set_value_t{}, static_cast<Args&&>(args)...);
        stdexec::set_value(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      void set_stopped(set_stopped_t) && noexcept {
        op_->result_->emplace(set_stopped_t{});
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      template <class Error>
        requires emplaceable<ResultVariant, decayed_tuple<set_error_t, Error>, set_error_t, Error>
      void set_error(set_error_t, Error&& error) && noexcept {
        op_->result_->emplace(set_error_t{}, static_cast<Error&&>(error));
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->receiver_));
      }

      auto get_env(get_env_t) const noexcept {
        auto with_never_stop_token = exec::sequence_receiver_stops_item(
          stdexec::get_env(op_->receiver_));
        if constexpr (same_as<decltype(with_never_stop_token), std::true_type>) {
          return exec::make_env(
            stdexec::get_env(op_->receiver_),
            exec::with(stdexec::get_stop_token, stdexec::never_stop_token{}));
        } else {
          return stdexec::get_env(op_->receiver_);
        }
      }
    };

    template <class Sender, class ItemReceiver, class ResultVariant, bool IsLockStep>
    struct item_operation : item_operation_base<ItemReceiver, ResultVariant, IsLockStep> {
      using base_type = item_operation_base<ItemReceiver, ResultVariant, IsLockStep>;
      using item_receiver_t = item_receiver<ItemReceiver, ResultVariant, IsLockStep>;
      connect_result_t<Sender, item_receiver_t> op_;

      item_operation(result_type<ResultVariant, IsLockStep>* parent, Sender&& sndr, ItemReceiver rcvr) noexcept(
        __nothrow_decay_copyable<ItemReceiver> && __nothrow_connectable<Sender, item_receiver_t>)
        : base_type{static_cast<ItemReceiver&&>(rcvr), parent}
        , op_{stdexec::connect(static_cast<Sender&&>(sndr), item_receiver_t{this})} {
      }

      void start(start_t) noexcept {
        stdexec::start(op_);
      }
    };

    template <class Sender, class ResultVariant, bool IsLockStep>
    struct item_sender {
      struct type;
    };

    template <class Sender, class ResultVariant, bool IsLockStep>
    struct item_sender<Sender, ResultVariant, IsLockStep>::type {
      using completion_signatures = stdexec::completion_signatures<set_value_t(), set_stopped_t()>;

      template <class Self, class Receiver>
      using operation_t =
        item_operation<__copy_cvref_t<Self, Sender>, Receiver, ResultVariant, IsLockStep>;

      template <class Receiver>
      using receiver_t = item_receiver<Receiver, ResultVariant, IsLockStep>;

      Sender sender_;
      result_type<ResultVariant, IsLockStep>* parent_;

      template <decays_to<type> Self, receiver Receiver>
        requires sender_to<copy_cvref_t<Self, Sender>, receiver_t<Receiver>>
      static auto connect(Self&& self, connect_t, Receiver rcvr) -> operation_t<Self, Receiver> {
        return {self.parent_, static_cast<Self&&>(self).sender_, static_cast<Receiver&&>(rcvr)};
      }
    };

    template <class Receiver, class ResultVariant, bool IsLockStep>
    struct operation_base : result_type<ResultVariant, IsLockStep> {
      [[no_unique_address]] Receiver receiver_;
    };

    template <class Receiver, class ResultVariant, bool IsLockStep>
    struct receiver {
      operation_base<Receiver, ResultVariant, IsLockStep>* op_;

      template <class Item>
      auto set_next(exec::set_next_t, Item&& item) noexcept(nothrow_decay_copyable<Item>) ->
        typename item_sender<decay_t<Item>, ResultVariant, IsLockStep>::type {
        return {static_cast<Item&&>(item), op_};
      }

      void set_value(set_value_t) && noexcept {
        op_->visit_result(static_cast<Receiver&&>(op_->receiver_));
      }

      void set_stopped(set_stopped_t) && noexcept {
        stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
      }

      template <class Error>
      void set_error(set_error_t, Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), static_cast<Error&&>(error));
      }

      env_of_t<Receiver> get_env(get_env_t) const noexcept {
        return stdexec::get_env(op_->receiver_);
      }
    };

    template <class Sender>
    using SenderEnv = exec::sequence_env_of_t<Sender>;

    template <class SenderEnv>
    using parallelism_type = decltype(exec::parallelism(__declval<SenderEnv>()));

    template <class Sender>
    static constexpr bool IsLockStep =
      same_as<parallelism_type<exec::sequence_env_of_t<Sender>>, exec::lock_step_t>;

    template <class Tag, class Sigs>
    using gather_types =
      __gather_signal< Tag, Sigs, __mbind_front_q<__decayed_tuple, Tag>, __q<__types>>;

    template <class Sigs>
    using result_variant_ = __minvoke<
      __mconcat<__nullable_variant_t>,
      gather_types<set_value_t, Sigs>,
      gather_types<set_error_t, Sigs>,
      gather_types<set_stopped_t, Sigs>>;

    template <class Sender, class Env>
    using result_variant_t = result_variant_<completion_signatures_of_t<Sender, Env>>;

    template <class Sender, class Receiver>
    struct operation
      : operation_base<Receiver, result_variant_t<Sender, env_of_t<Receiver>>, IsLockStep<Sender>> {
      using ResultVariant = result_variant_t<Sender, env_of_t<Receiver>>;
      using base_type = operation_base<Receiver, ResultVariant, IsLockStep<Sender>>;
      using receiver_t = receiver<Receiver, ResultVariant, IsLockStep<Sender>>;

      exec::subscribe_result_t<Sender, receiver_t> op_;

      operation(Sender&& sndr, Receiver rcvr) //
        noexcept(
          nothrow_decay_copyable<Receiver> //
          && exec::nothrow_subscribeable<Sender, receiver_t>)
        : base_type{{}, static_cast<Receiver&&>(rcvr)}
        , op_{exec::subscribe(static_cast<Sender&&>(sndr), receiver_t{this})} {
      }

      void start(start_t) noexcept {
        stdexec::start(op_);
      }
    };

    template <class Sequence>
    struct sender {
      struct type;
    };

    template <class Sequence>
    struct sender<Sequence>::type {
      using is_sender = void;

      template <class Self, class Receiver>
      using operation_t = operation<__copy_cvref_t<Self, Sequence>, Receiver>;

      template <class Self, class Receiver>
      using ResultVariant = result_variant_t<__copy_cvref_t<Self, Sequence>, env_of_t<Receiver>>;

      template <class Self, class Receiver>
      using receiver_t = receiver<Receiver, ResultVariant<Self, Receiver>, IsLockStep<Sequence>>;

      [[no_unique_address]] Sequence sequence_;

      template <decays_to<type> Self, stdexec::receiver Receiver>
        requires exec::sequence_sender_to<
          copy_cvref_t<Self, Sequence>,
          receiver_t<copy_cvref_t<Self, Sequence>, Receiver>>
      static auto connect(Self&& self, connect_t, Receiver rcvr) noexcept {
        return operation_t<Self, Receiver>{
          static_cast<Self&&>(self).sequence_, static_cast<Receiver&&>(rcvr)};
      }

      template <class Env>
      auto get_completion_signatures(get_completion_signatures_t, Env&&)
        -> __concat_completion_signatures_t<
          completion_signatures_of_t<Sequence, Env>,
          completion_signatures<set_stopped_t()>>;
    };

    struct last_t {
      template <exec::sequence_sender Sender>
      auto operator()(Sender&& seq) const noexcept(nothrow_decay_copyable<Sender>) ->
        typename sender<__decay_t<Sender>>::type {
        return {static_cast<Sender&&>(seq)};
      }

      template <stdexec::sender Sender>
        requires(!exec::sequence_sender<Sender>)
      auto operator()(Sender&& sndr) const noexcept {
        return static_cast<Sender&&>(sndr);
      }

      constexpr auto operator()() const noexcept -> binder_back<last_t> {
        return {{}, {}, {}};
      }
    };
  } // namespace last_

  using last_::last_t;
  inline constexpr last_t last{};
} // namespace sio