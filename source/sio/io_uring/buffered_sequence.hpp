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
#include "../sequence/sequence_concepts.hpp"

#include <stdexec/execution.hpp>

#include <exec/linux/io_uring_context.hpp>

#include <span>
#include <variant>

namespace sio {
  template <class Sender, class Receiver>
  struct buffered_sequence_op;

  template <class Sender, class ItemReceiver, class Receiver>
  struct buffered_item_op_base {
    buffered_sequence_op<Sender, Receiver>* parent_op_;
    [[no_unique_address]] ItemReceiver item_receiver_;
  };

  std::size_t
    advance_buffers(std::variant<::iovec, std::span<::iovec>>& buffers, std::size_t n) noexcept;

  bool buffers_is_empty(std::variant<::iovec, std::span<::iovec>> buffers) noexcept;

  template <class Sender, class ItemReceiver, class Receiver>
  struct buffered_item_receiver {
    buffered_item_op_base<Sender, ItemReceiver, Receiver>* op_;

    stdexec::env_of_t<ItemReceiver> get_env(stdexec::get_env_t) const noexcept {
      return stdexec::get_env(op_->item_receiver_);
    }

    void set_value(stdexec::set_value_t, std::size_t n) && noexcept {
      advance_buffers(op_->parent_op_->sender_.buffers_, n);
      stdexec::set_value(static_cast<ItemReceiver&&>(op_->item_receiver_), n);
    }

    void set_error(stdexec::set_error_t, std::error_code ec) && noexcept {
      stdexec::set_error(static_cast<ItemReceiver&&>(op_->item_receiver_), ec);
    }

    void set_stopped(stdexec::set_stopped_t) && noexcept {
      stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
    }
  };

  template <class Sender, class ItemReceiver, class Receiver>
  struct buffered_item_op : buffered_item_op_base<Sender, ItemReceiver, Receiver> {
    stdexec::connect_result_t<Sender, buffered_item_receiver<Sender, ItemReceiver, Receiver>> op_;

    buffered_item_op(
      buffered_sequence_op<Sender, Receiver>* parent_op,
      const Sender& sender,
      ItemReceiver item_receiver)
      : buffered_item_op_base<
        Sender,
        ItemReceiver,
        Receiver>{parent_op, static_cast<ItemReceiver&&>(item_receiver)}
      , op_{
          stdexec::connect(sender, buffered_item_receiver<Sender, ItemReceiver, Receiver>{this})} {
    }

    bool request_stop() noexcept {
      return op_.base().__stop_operation_.start();
    }

    void start(stdexec::start_t) noexcept {
      stdexec::start(op_);
    }
  };

  template <class Sender, class Receiver>
  struct buffered_item {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures_of_t<Sender>;

    buffered_sequence_op<Sender, Receiver>* parent_op_;

    template <stdexec::receiver ItemReceiver>
      requires stdexec::sender_to<Sender, buffered_item_receiver<Sender, ItemReceiver, Receiver>>
    buffered_item_op<Sender, ItemReceiver, Receiver>
      connect(stdexec::connect_t, ItemReceiver item_receiver) const noexcept {
      return buffered_item_op<Sender, ItemReceiver, Receiver>{
        parent_op_, parent_op_->sender_, static_cast<ItemReceiver&&>(item_receiver)};
    }
  };

  template <class Sender, class Receiver>
  struct buffered_next_receiver {
    using is_receiver = void;

    buffered_sequence_op<Sender, Receiver>* op_;

    void set_value(stdexec::set_value_t) && noexcept {
      int prev = op_->n_ops_.fetch_sub(1, std::memory_order_relaxed);
      if (prev != 1) {
        return;
      }
      if (buffers_is_empty(op_->sender_.buffers_)) {
        op_->callback_.reset();
        stdexec::set_value(static_cast<Receiver&&>(op_->receiver_));
        return;
      }
      try {
        int expected = 0;
        if (op_->n_ops_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          stdexec::start(op_->connect_next());
        }
      } catch (...) {
        op_->callback_.reset();
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), std::current_exception());
      }
    }

    void set_stopped(stdexec::set_stopped_t) && noexcept {
      op_->callback_.reset();
      exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->receiver_));
    }

    exec::make_env_t<
      stdexec::env_of_t<Receiver>,
      exec::with_t<exec::sequence_receiver_stops_item_t, std::true_type>>
      get_env(stdexec::get_env_t) const noexcept {
      return exec::make_env(
        stdexec::get_env(op_->receiver_),
        exec::with(exec::sequence_receiver_stops_item, std::true_type{}));
    }
  };

  template <class Sender, class Receiver>
  struct buffered_sequence_op {
    Receiver receiver_;
    Sender sender_;
    std::atomic<int> n_ops_{0};

    using next_op_t = stdexec::connect_result_t<
      exec::next_sender_of_t<Receiver, buffered_item<Sender, Receiver>>,
      buffered_next_receiver<Sender, Receiver>>;

    std::optional<next_op_t> next_op_;

    struct on_receiver_stop {
      buffered_sequence_op& op_;

      void operator()() const noexcept {
        op_.request_stop();
      }
    };

    using stop_token_type = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;
    using callback_type = typename stop_token_type::template callback_type<on_receiver_stop>;

    std::optional<callback_type> callback_;

    explicit buffered_sequence_op(Receiver receiver, Sender sndr)
      : receiver_(static_cast<Receiver&&>(receiver))
      , sender_(sndr) {
      connect_next();
    }

    void request_stop() noexcept
      // requires callable<request_stop_t, next_op_t&>
    {
      int prev = n_ops_.fetch_add(1, std::memory_order_relaxed);
      if (prev == 1) {
        bool submitted = next_op_->request_stop();
        if (!submitted && n_ops_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          stdexec::set_stopped(static_cast<Receiver&&>(receiver_));
        }
      }
    }

    decltype(auto) connect_next() {
      return next_op_.emplace(stdexec::__conv{[this] {
        return stdexec::connect(
          exec::set_next(receiver_, buffered_item<Sender, Receiver>{this}),
          buffered_next_receiver<Sender, Receiver>{this});
      }});
    }

    void start(stdexec::start_t) noexcept {
      int expected = 0;
      if (n_ops_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
        // if constexpr (callable<request_stop_t, next_op_t&>) {
          callback_.emplace(stdexec::__conv{[&] {
            return callback_type{
              stdexec::get_stop_token(stdexec::get_env(receiver_)), on_receiver_stop{*this}};
          }});
        // }
        stdexec::start(*next_op_); 
      }
    }
  };

  template <class Sender>
  struct buffered_sequence {
    using is_sender = exec::sequence_tag;

    Sender sender_;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_error_t(std::exception_ptr),
      stdexec::set_stopped_t()>;

    template <exec::sequence_receiver_of<completion_signatures> Receiver>
    buffered_sequence_op<Sender, Receiver> subscribe(exec::subscribe_t, Receiver rcvr) const
      noexcept(nothrow_move_constructible<Receiver>) {
      return buffered_sequence_op<Sender, Receiver>{static_cast<Receiver&&>(rcvr), sender_};
    }

    auto get_env(stdexec::get_env_t) const noexcept {
      return stdexec::get_env(sender_);
    }

    auto get_sequence_env(exec::get_sequence_env_t) const noexcept {
      return exec::make_env(exec::with(exec::parallelism, exec::lock_step));
    }
  };

  template <class Sender>
  buffered_sequence(Sender) -> buffered_sequence<Sender>;
}