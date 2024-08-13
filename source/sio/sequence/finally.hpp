/*
 * Copyright (c) 2024 Maikel Nadolski
 * Copyright (c) 2024 Xiaoming Zhang
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
#include "../sequence/sequence_concepts.hpp"

#include <exec/sequence_senders.hpp>
#include <stdexec/__detail/__receivers.hpp>
#include <stdexec/__detail/__transform_completion_signatures.hpp>

namespace sio {
  namespace finally_ {
    template <class Receiver>
    struct receiver_ref {
      using receiver_concept = stdexec::receiver_t;
      Receiver& receiver_;

      auto get_env() const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(receiver_);
      }

      void set_value() && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(receiver_));
      }

      template <class Error>
        requires callable<stdexec::set_error_t, Receiver&&, Error>
      void set_error(Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(receiver_), static_cast<Error&&>(error));
      }

      void set_stopped() && noexcept
        requires callable<stdexec::set_stopped_t, Receiver&&>
      {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(receiver_));
      }
    };

    template <class FinalSender, class Receiver>
    struct operation_base {
      Receiver receiver_;
      stdexec::connect_result_t<FinalSender, receiver_ref<Receiver>> final_op_;

      operation_base(FinalSender&& final, Receiver receiver)
        : receiver_{static_cast<Receiver&&>(receiver)}
        , final_op_(stdexec::connect(
            static_cast<FinalSender&&>(final),
            receiver_ref<Receiver>{receiver_})) {
      }
    };

    template <class FinalSender, class Receiver>
    struct initial_receiver {
      using receiver_concept = stdexec::receiver_t;
      operation_base<FinalSender, Receiver>* op_;

      auto get_env() const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(op_->receiver_);
      }

      template <class Item>
      friend auto tag_invoke(exec::set_next_t, initial_receiver& self, Item&& item)
        -> exec::next_sender_of_t<Receiver, Item> {
        return exec::set_next(self.op_->receiver_, static_cast<Item&&>(item));
      }

      void set_value() && noexcept {
        stdexec::start(op_->final_op_);
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        // TODO: store error
        stdexec::start(op_->final_op_);
      }

      void set_stopped() && noexcept {
        stdexec::start(op_->final_op_);
      }
    };

    template <class InitialSender, class FinalSender, class Receiver>
    struct operation : operation_base<FinalSender, Receiver> {
      using receiver_t = initial_receiver<FinalSender, Receiver>;
      exec::subscribe_result_t<InitialSender, receiver_t > first_op_;

      operation(InitialSender&& initial, FinalSender&& final, Receiver receiver)
        : operation_base<FinalSender, Receiver>(
            static_cast<FinalSender&&>(final),
            static_cast<Receiver&&>(receiver))
        , first_op_(exec::subscribe(static_cast<InitialSender&&>(initial), receiver_t{this})) {
      }

      void start() noexcept {
        stdexec::start(first_op_);
      }
    };

    template <class InitialSender, class FinalSender>
    struct sequence {
      using sender_concept = exec::sequence_sender_t;

      InitialSender initial_;
      FinalSender final_;

      template <decays_to<sequence> Self, stdexec::receiver Receiver>
      friend auto tag_invoke(exec::subscribe_t, Self&& self, Receiver receiver)
        -> operation<copy_cvref_t<Self, InitialSender>, copy_cvref_t<Self, FinalSender>, Receiver> {
        return {
          static_cast<Self&&>(self).initial_,
          static_cast<Self&&>(self).final_,
          static_cast<Receiver&&>(receiver)};
      }

      template <decays_to<sequence> Self, class Env>
      friend auto tag_invoke(exec::get_item_types_t, Self&&, Env&&) noexcept
        -> exec::item_types_of_t<copy_cvref_t<Self, InitialSender>, Env> {
        return {};
      }

      template <class Env>
      auto get_completion_signatures(Env&&) noexcept
        -> stdexec::transform_completion_signatures_of<
          FinalSender,
          Env,
          stdexec::completion_signatures<stdexec::set_stopped_t()>>;
    };

    struct finally_t {
      template <class Initial, class Final>
      sequence<decay_t<Initial>, decay_t<Final>> operator()(Initial&& init, Final&& final) const {
        return {static_cast<Initial&&>(init), static_cast<Final&&>(final)};
      }
    };
  }

  using finally_::finally_t;
  inline constexpr finally_t finally{};
}
