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

#include "../buffer_algorithms.hpp"
#include "../mutable_buffer_span.hpp"
#include "../const_buffer_span.hpp"
#include "../concepts.hpp"
#include "../sequence/sequence_concepts.hpp"
#include "sio/mutable_buffer.hpp"

#include <tuple>

#include <exec/sequence_senders.hpp>
#include <exec/__detail/__basic_sequence.hpp>

namespace sio {
  namespace buffered_sequence_ {

    template <class SenderFactory, class Buffer>
    struct sequence_op_base {
      SenderFactory factory_;
      Buffer buffer_;
      ::off_t offset_;

      auto make_sender() noexcept -> call_result_t<SenderFactory&, Buffer, ::off_t> {
        return this->factory_(this->buffer_, offset_);
      }

      void advance(std::size_t n) noexcept {
        buffer_ += n;
        if (offset_ != -1) {
          offset_ += n;
        }
      }
    };

    template <class SenderFactory, class Buffer, class ItemReceiver>
    struct item_operation_base {
      sequence_op_base<SenderFactory, Buffer>* sequence_op_;
      ItemReceiver item_receiver_;
    };

    template <class SenderFactory, class Buffer, class ItemReceiver>
    struct item_receiver {
      using receiver_concept = stdexec::receiver_t;
      item_operation_base<SenderFactory, Buffer, ItemReceiver>* op_;

      void set_value(std::size_t n) && noexcept {
        op_->sequence_op_->advance(n);
        stdexec::set_value(static_cast<ItemReceiver&&>(op_->item_receiver_), n);
      }

      void set_error(std::error_code ec) && noexcept {
        stdexec::set_error(
          static_cast<ItemReceiver&&>(op_->item_receiver_), static_cast<std::error_code&&>(ec));
      }

      void set_stopped() && noexcept {
        stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
      }

      auto get_env() const noexcept -> stdexec::env_of_t<ItemReceiver> {
        return stdexec::get_env(op_->item_receiver_);
      }
    };

    template <class SenderFactory, class Buffer, class ItemReceiver>
    struct item_operation : item_operation_base<SenderFactory, Buffer, ItemReceiver> {
      using base_type = item_operation_base<SenderFactory, Buffer, ItemReceiver>;
      using Sender = call_result_t<SenderFactory&, Buffer, ::off_t>;
      using item_receiver_t = item_receiver<SenderFactory, Buffer, ItemReceiver>;

      stdexec::connect_result_t<Sender, item_receiver_t> op_;

      item_operation(
        sequence_op_base<SenderFactory, Buffer>* sequence_op,
        ItemReceiver item_receiver)
        : base_type{sequence_op, static_cast<ItemReceiver&&>(item_receiver)}
        , op_{stdexec::connect(this->sequence_op_->make_sender(), item_receiver_t{this})} {
      }

      void start() & noexcept {
        stdexec::start(op_);
      }
    };

    template <class SenderFactory, class Buffer>
    struct item_sender {
      using sender_concept = stdexec::sender_t;
      using Sender = call_result_t<SenderFactory&, Buffer, ::off_t>;
      using completion_signatures = stdexec::completion_signatures_of_t<Sender>;

      sequence_op_base<SenderFactory, Buffer>* sequence_op_;

      template <stdexec::receiver ItemReceiver>
        requires stdexec::sender_to<Sender, item_receiver<SenderFactory, Buffer, ItemReceiver>>
      auto connect(ItemReceiver item_receiver) noexcept
        -> item_operation<SenderFactory, Buffer, ItemReceiver> {
        return {sequence_op_, static_cast<ItemReceiver&&>(item_receiver)};
      }
    };

    template <class SenderFactory, class Buffer, class Receiver>
    struct next_receiver;

    template <class SenderFactory, class Buffer, class Receiver>
    struct sequence_op : sequence_op_base<SenderFactory, Buffer> {
      using base_type = sequence_op_base<SenderFactory, Buffer>;

      using item_sender_t = item_sender<SenderFactory, Buffer>;
      using next_sender_t = exec::next_sender_of_t<Receiver, item_sender_t>;
      using next_receiver_t = next_receiver<SenderFactory, Buffer, Receiver>;
      using next_op_t = stdexec::connect_result_t< next_sender_t, next_receiver_t>;

      std::optional<next_op_t> next_op_;
      Receiver receiver_;

      sequence_op(SenderFactory factory, Buffer buffer, ::off_t offset, Receiver receiver)
        : base_type(static_cast<SenderFactory&&>(factory), buffer, offset)
        , receiver_(static_cast<Receiver&&>(receiver)) {
        connect_next();
      }

      decltype(auto) connect_next() {
        return next_op_.emplace(stdexec::__conv{[this] {
          next_sender_t next = exec::set_next(receiver_, item_sender_t{this});
          return stdexec::connect(static_cast<next_sender_t&&>(next), next_receiver_t{this});
        }});
      }

      void start() & noexcept {
        stdexec::start(*next_op_);
      }
    };

    template <class SenderFactory, class Buffer, class Receiver>
    struct next_receiver {
      using receiver_concept = stdexec::receiver_t;

      sequence_op<SenderFactory, Buffer, Receiver>* sequence_op_;

      void set_value() && noexcept {
        if (sequence_op_->buffer_.size() == 0) {
          stdexec::set_value(static_cast<Receiver&&>(sequence_op_->receiver_));
          return;
        }
        try {
          stdexec::start(sequence_op_->connect_next());
        } catch (...) {
          stdexec::set_error(
            static_cast<Receiver&&>(sequence_op_->receiver_), std::current_exception());
        }
      }

      void set_stopped() && noexcept {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(sequence_op_->receiver_));
      }

      auto get_env() const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(sequence_op_->receiver_);
      }
    };

    template <class Receiver>
    struct subscribe_fn {
      Receiver& rcvr_;

      template <class SenderFactory, class Buffer>
        requires stdexec::sender_to<
                   exec::next_sender_of_t<Receiver, item_sender<SenderFactory, Buffer>>,
                   next_receiver<SenderFactory, Buffer, Receiver>>
      auto operator()(stdexec::__ignore, std::tuple<SenderFactory, Buffer, ::off_t>&& data) noexcept
        -> sequence_op< SenderFactory, Buffer, Receiver> {
        return {
          static_cast<SenderFactory&&>(std::get<0>(data)),
          static_cast<Buffer&&>(std::get<1>(data)),
          std::get<2>(data),
          static_cast<Receiver&&>(rcvr_)};
      }
    };


    template <class Buffer>
    concept single_buffer = decays_to<mutable_buffer, Buffer> || decays_to<const_buffer, Buffer>;

    struct buffered_sequence_t {
      template <class SenderFactory, class Buffer>
        requires single_buffer<Buffer>
      auto operator()(SenderFactory factory, Buffer buffer, ::off_t offset = -1) const
        -> stdexec::__well_formed_sender auto {
        return exec::make_sequence_expr<buffered_sequence_t>(
          std::tuple{static_cast<SenderFactory&&>(factory), static_cast<Buffer&&>(buffer), offset});
      }

      template <class Self, class Receiver>
      static auto subscribe(Self&& self, Receiver rcvr) noexcept(
        stdexec::__nothrow_callable<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>>)
        -> stdexec::__call_result_t<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>> {
        return stdexec::__sexpr_apply(static_cast<Self&&>(self), subscribe_fn< Receiver>{rcvr});
      }

      template <stdexec::sender_expr_for<buffered_sequence_t> Self, class Env>
      static auto get_completion_signatures(Self&&, Env&&) noexcept
        -> stdexec::completion_signatures<
          stdexec::set_value_t(),
          stdexec::set_error_t(std::exception_ptr),
          stdexec::set_stopped_t()> {
        return {};
      }

      template <stdexec::sender_expr_for<buffered_sequence_t> Self, class Env>
      static auto get_item_types(Self&&, Env&&) noexcept
        -> exec::item_types<item_sender<
          std::tuple_element_t<0, stdexec::__data_of<Self>>,
          std::tuple_element_t<1, stdexec::__data_of<Self> >>> {
        return {};
      }

      auto get_sequence_env() const noexcept {
        return exec::make_env(exec::with(exec::parallelism, exec::lock_step));
      }
    };
  }

  using buffered_sequence_::buffered_sequence_t;
  inline constexpr buffered_sequence_t buffered_sequence{};
}
