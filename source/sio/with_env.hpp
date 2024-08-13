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

#include <exec/env.hpp>
#include <stdexec/__detail/__receivers.hpp>
#include <stdexec/__detail/__senders_core.hpp>

namespace sio {
  namespace with_env_ {

    template <class Env, class Receiver>
    struct operation_base {
      Env env;
      Receiver receiver;
    };

    template <class Env, class Receiver>
    struct receiver {
      using receiver_concept = stdexec::receiver_t;

      operation_base<Env, Receiver>* op_;

      auto get_env() const noexcept {
        return stdexec::__env::__join(op_->env, stdexec::get_env(op_->receiver));
      }

      template <class... Args>
      void set_value(Args&&... args) && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(op_->receiver), static_cast<Args&&>(args)...);
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver), static_cast<Error&&>(error));
      }

      void set_stopped() && noexcept {
        stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver));
      }
    };

    template <class Env, class Sender, class Receiver>
    struct operation : operation_base<Env, Receiver> {
      stdexec::connect_result_t<Sender, receiver<Env, Receiver>> op_;

      operation(Env env, Sender&& sender, Receiver rcvr)
        : operation_base<Env, Receiver>{static_cast<Env&&>(env), static_cast<Receiver&&>(rcvr)}
        , op_{stdexec::connect(
            static_cast<Sender&&>(sender),
            with_env_::receiver<Env, Receiver>{this})} {
      }

      void start() noexcept {
        stdexec::start(op_);
      }
    };

    template <class Env, class Sender>
    struct sender {
      using sender_concept = stdexec::sender_t;
      using completion_signatures = stdexec::completion_signatures_of_t<Sender, Env>;

      Env env_;
      Sender sender_;

      template <stdexec::receiver_of<completion_signatures> Receiver>
        requires stdexec::sender_to<Sender, receiver<Env, Receiver>>
      auto connect(Receiver rcvr) -> operation<Env, Sender, Receiver> {
        return {env_, static_cast<Sender&&>(sender_), static_cast<Receiver&&>(rcvr)};
      }
    };

    struct with_env_t {
      template <class Env, class Sender>
      auto operator()(Env env, Sender sender) const -> with_env_::sender<Env, Sender> {
        return {static_cast<Env&&>(env), static_cast<Sender&&>(sender)};
      }
    };
  }

  using with_env_::with_env_t;
  inline constexpr with_env_t with_env{};

}
