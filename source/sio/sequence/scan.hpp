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

#include <functional>
#include <exec/sequence_senders.hpp>
#include <exec/__detail/__basic_sequence.hpp>
#include <stdexec/execution.hpp>
#include <stdexec/functional.hpp>
#include <tuple>

#include "../concepts.hpp"
#include "./sequence_concepts.hpp"

namespace sio {
  namespace scan_ {

    template <class Tp, class Fn, bool IsLockStep>
    struct scan_data {
      Tp value_;
      Fn fn_;
      std::mutex mutex_{};

      template <class... Args>
        requires callable<Fn&, Tp&, Args...>
      auto emplace(Args&&... args) noexcept(nothrow_callable<Fn&, Tp&, Args...>) -> Tp {
        std::scoped_lock lock{mutex_};
        value_ = fn_(value_, static_cast<Args&&>(args)...);
        return value_;
      }
    };

    template <class Tp, class Fn>
    struct scan_data<Tp, Fn, true> {
      Tp value_;
      Fn fn_;

      template <class... Args>
        requires callable<Fn&, Tp&, Args...>
      auto emplace(Args&&... args) noexcept(nothrow_callable<Fn&, Tp&, Args...>) -> Tp {
        value_ = fn_(value_, static_cast<Args&&>(args)...);
        return value_;
      }
    };

    template <class ItemReceiver, class Tp, class Fn, bool IsLockStep>
    struct item_operation_base {
      [[no_unique_address]] ItemReceiver rcvr_;
      scan_data<Tp, Fn, IsLockStep>* data_;
    };

    template <class ItemReceiver, class Tp, class Fn, bool IsLockStep>
    struct item_receiver {
      using receiver_concept = stdexec::receiver_t;

      item_operation_base<ItemReceiver, Tp, Fn, IsLockStep>* op_;

      template <class... Args>
      void set_value(Args&&... args) && noexcept {
        try {
          Tp value = op_->data_->emplace(static_cast<Args&&>(args)...);
          stdexec::set_value(static_cast<ItemReceiver&&>(op_->rcvr_), static_cast<Tp&&>(value));
        } catch (...) {
          stdexec::set_error(static_cast<ItemReceiver&&>(op_->rcvr_), std::current_exception());
        }
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        stdexec::set_error(static_cast<ItemReceiver&&>(op_->rcvr_), static_cast<Error&&>(error));
      }

      void set_stopped() && noexcept {
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->rcvr_));
      }

      auto get_env() const noexcept -> stdexec::env_of_t<ItemReceiver> {
        return stdexec::get_env(op_->rcvr_);
      }
    };

    template <class ItemSender, class ItemReceiver, class Tp, class Fn, bool IsLockStep>
    struct item_operation : item_operation_base<ItemReceiver, Tp, Fn, IsLockStep> {
      using item_receiver_t = item_receiver<ItemReceiver, Tp, Fn, IsLockStep>;
      using base_t = item_operation_base< ItemReceiver, Tp, Fn, IsLockStep>;

      stdexec::connect_result_t<ItemSender, item_receiver_t > op_;

      item_operation(ItemSender&& sndr, ItemReceiver rcvr, scan_data<Tp, Fn, IsLockStep>* data)
        : base_t{static_cast<ItemReceiver&&>(rcvr), data}
        , op_{stdexec::connect(static_cast<ItemSender&&>(sndr), item_receiver_t{this})} {
      }

      void start() noexcept {
        stdexec::start(op_);
      }
    };

    template <class ItemSender, class Tp, class Fn, class IsLockStep>
    struct item_sender {
      using sender_concept = stdexec::sender_t;

      ItemSender sndr_;
      scan_data<Tp, Fn, IsLockStep::value>* data_;

      template < class ItemReceiver>
      auto connect(ItemReceiver rcvr) //
        -> item_operation<ItemSender, ItemReceiver, Tp, Fn, IsLockStep::value> {
        return {static_cast<ItemSender&&>(sndr_), static_cast<ItemReceiver&&>(rcvr), data_};
      }

      template <class Self, class Env>
      static auto get_completion_signatures(Self&&, Env&&)
        -> stdexec::transform_completion_signatures_of<
          copy_cvref_t<Self, ItemSender>,
          Env,
          stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>,
          stdexec::__mconst<stdexec::completion_signatures<stdexec::set_value_t(Tp)>>::template __f>;
    };

    template <class Receiver, class Tp, class Fn, bool IsLockStep>
    struct operation_base {
      [[no_unique_address]] Receiver rcvr_;
      scan_data<Tp, Fn, IsLockStep> data_;
    };

    template <class Receiver, class Tp, class Fn, bool IsLockStep>
    struct receiver {
      using receiver_concept = stdexec::receiver_t;

      template <class ItemSender>
      using item_sender_t = item_sender<decay_t<ItemSender>, Tp, Fn, stdexec::__mbool<IsLockStep>>;

      operation_base<Receiver, Tp, Fn, IsLockStep>* op_;

      template <class ItemSender>
      friend auto tag_invoke(exec::set_next_t, receiver& self, ItemSender&& sndr)
        -> exec::next_sender_of_t< Receiver, item_sender_t<ItemSender>> {
        return exec::set_next(
          self.op_->rcvr_,
          receiver::item_sender_t<ItemSender>{static_cast<ItemSender&&>(sndr), &self.op_->data_});
      }

      void set_value() && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), static_cast<Error&&>(error));
      }

      void set_stopped() && noexcept {
        stdexec::set_stopped(static_cast<Receiver&&>(op_->rcvr_));
      }

      auto get_env() const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(op_->rcvr_);
      }
    };

    template <class Sender, class Receiver, class Tp, class Fn, bool IsLockStep>
    struct operation : operation_base<Receiver, Tp, Fn, IsLockStep> {
      using receiver_t = scan_::receiver<Receiver, Tp, Fn, IsLockStep>;
      using base_t = operation_base<Receiver, Tp, Fn, IsLockStep>;
      using scan_data_t = scan_data<Tp, Fn, IsLockStep>;

      exec::subscribe_result_t<Sender, receiver_t> op_;

      operation(Sender&& sndr, Receiver rcvr, Tp init, Fn fn)
        : base_t{static_cast<Receiver&&>(rcvr), scan_data_t{static_cast<Tp&&>(init), static_cast<Fn&&>(fn)}}
        , op_{exec::subscribe(
            static_cast<Sender&&>(sndr),
            receiver_t{this})} {
      }

      void start() noexcept {
        stdexec::start(op_);
      }
    };

    template <class Receiver>
    struct subscribe_fn {
      Receiver& rcvr;

      template <class Env>
      using parallelism_type = decltype(exec::parallelism(stdexec::__declval<Env>()));

      template <class Sender>
      static constexpr bool IsLockStep =
        std::same_as<parallelism_type<exec::sequence_env_of_t<Sender>>, exec::lock_step_t>;

      template <class State, class Child>
      auto operator()(stdexec::__ignore, State&& state, Child&& child) noexcept
        -> operation<
          Child,
          Receiver,
          std::tuple_element_t<0, stdexec::__decay_t<State>>,
          std::tuple_element_t<1, stdexec::__decay_t<State>>,
          IsLockStep<Child>> {
        return {
          static_cast<Child&&>(child),
          static_cast<Receiver&&>(rcvr),
          std::move(std::get<0>(state)),
          std::move(std::get<1>(state))};
      }
    };

    struct scan_t {
      template <class Sender, class Tp, class Fn = std::plus<>>
      auto operator()(Sender&& sndr, Tp init, Fn fun = Fn()) const //
        -> stdexec::__well_formed_sender auto {
        auto domain = stdexec::__get_early_domain(sndr);
        return stdexec::transform_sender(
          domain,
          exec::make_sequence_expr<scan_t>(
            std::make_tuple(static_cast<Tp&&>(init), static_cast<Fn&&>(fun)),
            static_cast<Sender&&>(sndr)));
      }

      template <stdexec::sender_expr_for<scan_t> Self, stdexec::receiver Receiver>
      static auto subscribe(Self&& self, Receiver rcvr) noexcept(
        stdexec::__nothrow_invocable<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>>)
        -> stdexec::__call_result_t<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>> {
        return stdexec::__sexpr_apply(static_cast<Self&&>(self), subscribe_fn<Receiver>{rcvr});
      }

      template <stdexec::sender_expr_for<scan_t> Self, class Env>
      static auto get_completion_signatures(Self&&, Env&&) noexcept
        -> exec::__sequence_completion_signatures_of_t<stdexec::__child_of<Self>, Env>;

      template <class Env>
      using parallelism_type = decltype(exec::parallelism(stdexec::__declval<Env>()));

      template <class Sender>
      static constexpr bool IsLockStep =
        std::same_as<parallelism_type<exec::sequence_env_of_t<Sender>>, exec::lock_step_t>;

      template <class Self, class Env>
      using item_type = exec::item_types< item_sender<
        exec::item_sender_t<exec::item_types_of_t<stdexec::__child_of<Self>, Env>>,
        std::tuple_element_t<0, stdexec::__decay_t<stdexec::__data_of<Self>>>,
        std::tuple_element_t<1, stdexec::__decay_t<stdexec::__data_of<Self>>>,
        stdexec::__mbool<IsLockStep<stdexec::__child_of<Self>>>> >;

      template <stdexec::sender_expr_for<scan_t> Self, class Env>
      static auto get_item_types(Self&&, Env&&) noexcept -> item_type<Self, Env>;
    };
  } // namespace scan_

  using scan_::scan_t;
  inline constexpr scan_t scan{};
} // namespace sio
