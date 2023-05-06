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

#include "./concepts.hpp"
#include "./sequence_concepts.hpp"

namespace sio {
  namespace transform_ {
    using namespace stdexec;

    template <class Receiver, class Adaptor>
    struct operation_base {
      Receiver receiver_;
      Adaptor adaptor_;
    };

    template <class Receiver, class Adaptor>
    struct receiver {
      operation_base<Receiver, Adaptor>* op_;

      template <class Item>
        requires callable<Adaptor&, Item>
              && callable<exec::set_next_t, Receiver&, call_result_t<Adaptor&, Item>>
      auto set_next(exec::set_next_t, Item&& item) {
        return exec::set_next(op_->receiver_, op_->adaptor_(static_cast<Item&&>(item)));
      }

      void set_value(set_value_t) && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(op_->receiver_));
      }

      void set_stopped(set_stopped_t) && noexcept
        requires callable<set_stopped_t, Receiver&&>
      {
        stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
      }

      template <class Error>
        requires callable<set_error_t, Receiver&&, Error>
      void set_error(set_error_t, Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), static_cast<Error&&>(error));
      }

      env_of_t<Receiver> get_env(get_env_t) const noexcept {
        return stdexec::get_env(op_->receiver_);
      }
    };

    template <class Sender, class Receiver, class Adaptor>
    struct operation : operation_base<Receiver, Adaptor> {
      exec::sequence_connect_result_t<Sender, receiver<Receiver, Adaptor>> op_;

      operation(Sender&& sndr, Receiver rcvr, Adaptor adaptor)
        : operation_base<
          Receiver,
          Adaptor>{static_cast<Receiver&&>(rcvr), static_cast<Adaptor&&>(adaptor)}
        , op_{exec::sequence_connect(
            static_cast<Sender&&>(sndr),
            receiver<Receiver, Adaptor>{this})} {
      }

      void start(start_t) noexcept {
        stdexec::start(op_);
      }
    };

    template <class Sender, class Adaptor>
    struct sender {
      using is_sender = exec::sequence_tag;

      Sender sender_;
      Adaptor adaptor_;

      template <class Self, class Env>
      using some_sender = copy_cvref_t<
        Self,
        exec::__sequence_sndr::__unspecified_sender_of<
          completion_signatures_of_t<copy_cvref_t<Self, Sender>, Env>>>;

      template <class Self, class Env>
      using completion_sigs_t =
        completion_signatures_of_t<call_result_t<Adaptor&, some_sender<Self, Sender>>, Env>;

      template <decays_to<sender> Self, stdexec::receiver Receiver>
        requires exec::sequence_receiver_of<Receiver, completion_sigs_t<Self, env_of_t<Receiver>>>
              && exec::sequence_sender_to<copy_cvref_t<Self, Sender>, receiver<Receiver, Adaptor>>
      static auto sequence_connect(Self&& self, exec::sequence_connect_t, Receiver rcvr)
        -> operation<copy_cvref_t<Self, Sender>, Receiver, Adaptor> {
        return {
          static_cast<Self&&>(self).sender_,
          static_cast<Receiver&&>(rcvr),
          static_cast<Self&&>(self).adaptor_};
      }

      template <__decays_to<sender> Self, class Env>
      static auto get_completion_signatures(Self&& self, get_completion_signatures_t, Env&& env)
        -> completion_sigs_t<Self, Env>;

      exec::sequence_env_of_t<Sender> get_sequence_env(exec::get_sequence_env_t) const noexcept {
        return exec::get_sequence_env(sender_);
      }
    };

    struct transform_each_t {
      template <exec::sequence_sender Sender, class Adaptor>
      auto operator()(Sender&& sndr, Adaptor&& adaptor) const
        noexcept(nothrow_decay_copyable<Sender> //
                   && nothrow_decay_copyable<Adaptor>)
          -> sender<decay_t<Sender>, decay_t<Adaptor>> {
        return {static_cast<Sender&&>(sndr), static_cast<Adaptor&&>(adaptor)};
      }

      template <stdexec::sender Sender, class Adaptor>
        requires(!exec::sequence_sender<Sender>)
      auto operator()(Sender&& sndr, Adaptor&& adaptor) const
        noexcept(nothrow_callable<Adaptor, Sender>) -> call_result_t<Adaptor, Sender> {
        return static_cast<Adaptor&&>(adaptor)(static_cast<Sender&&>(sndr));
      }

      template <class Adaptor>
      constexpr auto operator()(Adaptor adaptor) const noexcept
        -> binder_back<transform_each_t, Adaptor> {
        return {{}, {}, {static_cast<Adaptor&&>(adaptor)}};
      }
    };
  }

  using transform_::transform_each_t;
  inline constexpr transform_each_t transform_each{};
}