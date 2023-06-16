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
#include "../request_stop.hpp"
#include "./sequence_concepts.hpp"

#include <functional>

namespace sio {
  namespace scan_ {

    template <class Tp, class Fn, bool IsLockStep>
    struct scan_data {
      Tp value_;
      Fn fn_;
      std::mutex mutex_{};

      template <class... Args>
        requires callable<Fn&, Tp&, Args...>
      Tp emplace(Args&&... args) noexcept(nothrow_callable<Fn&, Tp&, Args...>) {
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
      Tp emplace(Args&&... args) noexcept(nothrow_callable<Fn&, Tp&, Args...>) {
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
      using is_receiver = void;

      item_operation_base<ItemReceiver, Tp, Fn, IsLockStep>* op_;

      stdexec::env_of_t<ItemReceiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }

      template <class... Args>
      void set_value(stdexec::set_value_t, Args&&... args) && noexcept {
        try {
          Tp value = op_->data_->emplace(static_cast<Args&&>(args)...);
          stdexec::set_value(static_cast<ItemReceiver&&>(op_->rcvr_), static_cast<Tp&&>(value));
        } catch (...) {
          stdexec::set_error(static_cast<ItemReceiver&&>(op_->rcvr_), std::current_exception());
        }
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_error(static_cast<ItemReceiver&&>(op_->rcvr_), static_cast<Error&&>(error));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->rcvr_));
      }
    };

    template <class ItemSender, class ItemReceiver, class Tp, class Fn, bool IsLockStep>
    struct item_operation : item_operation_base<ItemReceiver, Tp, Fn, IsLockStep> {
      stdexec::connect_result_t<ItemSender, item_receiver<ItemReceiver, Tp, Fn, IsLockStep>> op_;

      explicit item_operation(
        ItemSender&& sndr,
        ItemReceiver rcvr,
        scan_data<Tp, Fn, IsLockStep>* data)
        : item_operation_base<
          ItemReceiver,
          Tp,
          Fn,
          IsLockStep>{static_cast<ItemReceiver&&>(rcvr), data}
        , op_{stdexec::connect(
            static_cast<ItemSender&&>(sndr),
            item_receiver<ItemReceiver, Tp, Fn, IsLockStep>{this})} {
      }

      bool request_stop() noexcept {
        return op_.request_stop();
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(op_);
      }
    };

    template <class ItemSender, class Tp, class Fn, bool IsLockStep>
    struct item_sender {
      using is_sender = void;

      ItemSender sndr_;
      scan_data<Tp, Fn, IsLockStep>* data_;

      template <decays_to<item_sender> Self, class ItemReceiver>
      static item_operation<copy_cvref_t<Self, ItemSender>, ItemReceiver, Tp, Fn, IsLockStep>
        connect(Self&& self, stdexec::connect_t, ItemReceiver rcvr) {
        return item_operation<copy_cvref_t<Self, ItemSender>, ItemReceiver, Tp, Fn, IsLockStep>{
          static_cast<Self&&>(self).sndr_,
          static_cast<ItemReceiver&&>(rcvr),
          static_cast<Self&&>(self).data_};
      }

      template <class Self, class Env>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> stdexec::make_completion_signatures<
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
      using is_receiver = void;

      operation_base<Receiver, Tp, Fn, IsLockStep>* op_;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }

      template <class ItemSender>
      auto set_next(exec::set_next_t, ItemSender&& sndr) {
        return exec::set_next(
          op_->rcvr_,
          item_sender<decay_t<ItemSender>, Tp, Fn, IsLockStep>{
            static_cast<ItemSender&&>(sndr), &op_->data_});
      }

      void set_value(stdexec::set_value_t) && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), static_cast<Error&&>(error));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        stdexec::set_stopped(static_cast<Receiver&&>(op_->rcvr_));
      }
    };

    template <class Sender, class Receiver, class Tp, class Fn, bool IsLockStep>
    struct operation : operation_base<Receiver, Tp, Fn, IsLockStep> {
      exec::subscribe_result_t<Sender, receiver<Receiver, Tp, Fn, IsLockStep>> op_;

      explicit operation(Sender&& sndr, Receiver rcvr, Tp init, Fn fn)
        : operation_base<Receiver, Tp, Fn, IsLockStep>{static_cast<Receiver&&>(rcvr), scan_data<Tp, Fn, IsLockStep>{static_cast<Tp&&>(init), static_cast<Fn&&>(fn)}}
        , op_{exec::subscribe(
            static_cast<Sender&&>(sndr),
            receiver<Receiver, Tp, Fn, IsLockStep>{this})} {
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(op_);
      }
    };

    template <class Sender, class Tp, class Fn>
    struct sequence {
      using is_sender = exec::sequence_tag;

      Sender sndr_;

      template <class SenderEnv>
      using parallelism_type = decltype(exec::parallelism(stdexec::__declval<SenderEnv>()));

      static constexpr bool IsLockStep =
        std::same_as<parallelism_type<exec::sequence_env_of_t<Sender>>, exec::lock_step_t>;


      Tp init_;
      Fn fn_;

      explicit sequence(Sender sndr, Tp init, Fn fn)
        : sndr_{static_cast<Sender&&>(sndr)}
        , init_{static_cast<Tp&&>(init)}
        , fn_{static_cast<Fn&&>(fn)} {
      }

      template <decays_to<sequence> Self, stdexec::receiver Receiver>
      static operation<copy_cvref_t<Self, Sender>, Receiver, Tp, Fn, IsLockStep>
        subscribe(Self&& self, exec::subscribe_t, Receiver rcvr) {
        return operation<copy_cvref_t<Self, Sender>, Receiver, Tp, Fn, IsLockStep>{
          static_cast<Self&&>(self).sndr_,
          static_cast<Receiver&&>(rcvr),
          static_cast<Self&&>(self).init_,
          static_cast<Self&&>(self).fn_};
      }

      template <class Env>
      auto get_completion_signatures(stdexec::get_completion_signatures_t, Env&&) const
        -> stdexec::__msuccess_or_t<stdexec::__try_make_completion_signatures<
          Sender,
          Env,
          stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>,
          stdexec::__mconst<stdexec::completion_signatures<stdexec::set_value_t(Tp)>> >>;

      stdexec::env_of_t<Sender> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(sndr_);
      }

      exec::sequence_env_of_t<Sender> get_sequence_env(exec::get_sequence_env_t) const noexcept {
        return exec::get_sequence_env(sndr_);
      }
    };

    struct scan_t {
      template <class Sender, class Tp, class Fn = std::plus<>>
      sequence<decay_t<Sender>, Tp, Fn> operator()(Sender&& sndr, Tp init, Fn fun = Fn()) const {
        return sequence<decay_t<Sender>, Tp, Fn>{
          static_cast<Sender&&>(sndr), static_cast<Tp&&>(init), static_cast<Fn&&>(fun)};
      }
    };
  } // namespace scan_

  using scan_::scan_t;
  inline constexpr scan_t scan{};
} // namespace sio