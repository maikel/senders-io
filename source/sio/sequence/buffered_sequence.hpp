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

#include <exec/linux/io_uring_context.hpp>

#include <span>
#include <variant>

namespace sio {
  mutable_buffer_span to_buffer_sequence(const mutable_buffer&);
  const_buffer_span to_buffer_sequence(const const_buffer&);
  mutable_buffer_span to_buffer_sequence(const std::span<mutable_buffer>&);
  const_buffer_span to_buffer_sequence(const std::span<const_buffer>&);

  template <class Buffer>
  using buffer_sequence_of_t = decltype(to_buffer_sequence(std::declval<Buffer>()));

  template <class Buffer>
  class buffer_span;

  template <class Buffer>
    requires std::same_as<Buffer, mutable_buffer> || std::same_as<Buffer, const_buffer>
  class buffer_span<Buffer> {
    Buffer buffer_{};
    ::off_t offset_{-1};

   public:
    buffer_span() = default;

    explicit buffer_span(Buffer buffer, ::off_t offset = -1) noexcept
      : buffer_{buffer}
      , offset_{offset} {
    }

    buffer_sequence_of_t<Buffer> data() const noexcept {
      if (buffer_.size() == 0) {
        return buffer_sequence_of_t<Buffer>{};
      }
      return buffer_sequence_of_t<Buffer>{&buffer_, 1};
    }

    ::off_t offset() const noexcept {
      return offset_;
    }

    void advance(std::size_t n) noexcept {
      if (n < buffer_.size()) {
        buffer_ += n;
      } else {
        buffer_ = Buffer{};
      }
      if (offset_ != -1) {
        offset_ += n;
      }
    }
  };

  template <class Buffer>
    requires std::same_as<Buffer, mutable_buffer> || std::same_as<Buffer, const_buffer>
  class buffer_span<std::span<Buffer>> {
    std::span<Buffer> buffers_{};
    ::off_t offset_{-1};

   public:
    buffer_span() = default;

    explicit buffer_span(std::span<Buffer> buffers, ::off_t offset = -1) noexcept
      : buffers_{buffers}
      , offset_{offset} {
    }

    buffer_sequence_of_t<Buffer> data() const noexcept {
      return buffer_sequence_of_t<Buffer>{buffers_};
    }

    ::off_t offset() const noexcept {
      return offset_;
    }

    void advance(std::size_t n) noexcept {
      while (buffers_.size() > 0 && n > 0) {
        Buffer& buffer = buffers_.front();
        if (n < buffer.size()) {
          buffer += n;
          break;
        } else {
          n -= buffer.size();
          buffers_ = buffers_.subspan(1);
        }
      }
      if (offset_ != -1) {
        offset_ += n;
      }
    }
  };

  template <class SenderFactory, class Buffer>
  struct buffered_sequence_op_base {
    SenderFactory sender_factory_;
    buffer_span<Buffer> buffer_;

    call_result_t<SenderFactory&, buffer_sequence_of_t<Buffer>, ::off_t> make_sender() noexcept {
      return sender_factory_(buffer_.data(), buffer_.offset());
    }
  };

  template <class SenderFactory, class Buffer, class ItemReceiver>
  struct buffered_item_op_base {
    buffered_sequence_op_base<SenderFactory, Buffer>* parent_op_;
    ItemReceiver item_receiver_;
  };

  template <class SenderFactory, class Buffer, class ItemReceiver>
  struct buffered_item_receiver {
    buffered_item_op_base<SenderFactory, Buffer, ItemReceiver>* op_;

    stdexec::env_of_t<ItemReceiver> get_env(stdexec::get_env_t) const noexcept {
      return stdexec::get_env(op_->item_receiver_);
    }

    void set_value(stdexec::set_value_t, std::size_t n) && noexcept {
      op_->parent_op_->buffer_.advance(n);
      stdexec::set_value(static_cast<ItemReceiver&&>(op_->item_receiver_), std::move(n));
    }

    void set_error(stdexec::set_error_t, std::error_code ec) && noexcept {
      stdexec::set_error(static_cast<ItemReceiver&&>(op_->item_receiver_), std::move(ec));
    }

    void set_stopped(stdexec::set_stopped_t) && noexcept {
      stdexec::set_stopped(static_cast<ItemReceiver&&>(op_->item_receiver_));
    }
  };

  template <class SenderFactory, class Buffer, class ItemReceiver>
  struct buffered_item_op : buffered_item_op_base<SenderFactory, Buffer, ItemReceiver> {
    using Sender = call_result_t<SenderFactory&, buffer_sequence_of_t<Buffer>, ::off_t>;
    stdexec::connect_result_t<Sender, buffered_item_receiver<SenderFactory, Buffer, ItemReceiver>>
      op_;

    buffered_item_op(
      buffered_sequence_op_base<SenderFactory, Buffer>* parent_op,
      ItemReceiver item_receiver)
      : buffered_item_op_base<
        SenderFactory,
        Buffer,
        ItemReceiver>{parent_op, static_cast<ItemReceiver&&>(item_receiver)}
      , op_{stdexec::connect(
          this->parent_op_->make_sender(),
          buffered_item_receiver<SenderFactory, Buffer, ItemReceiver>{this})} {
    }

    void start(stdexec::start_t) noexcept {
      stdexec::start(op_);
    }
  };

  template <class SenderFactory, class Buffer>
  struct buffered_item {
    using is_sender = void;
    using Sender = call_result_t<SenderFactory&, buffer_sequence_of_t<Buffer>, ::off_t>;

    using completion_signatures = stdexec::completion_signatures_of_t<Sender>;

    buffered_sequence_op_base<SenderFactory, Buffer>* parent_op_;

    template <stdexec::receiver ItemReceiver>
      requires stdexec::
        sender_to<Sender, buffered_item_receiver<SenderFactory, Buffer, ItemReceiver>>
      buffered_item_op<SenderFactory, Buffer, ItemReceiver>
      connect(stdexec::connect_t, ItemReceiver item_receiver) const noexcept {
      return buffered_item_op<SenderFactory, Buffer, ItemReceiver>{
        parent_op_, static_cast<ItemReceiver&&>(item_receiver)};
    }
  };

  template <class SenderFactory, class Buffer, class Receiver>
  struct buffered_sequence_op;

  template <class SenderFactory, class Buffer, class Receiver>
  struct buffered_next_receiver {
    using is_receiver = void;

    buffered_sequence_op<SenderFactory, Buffer, Receiver>* op_;

    void set_value(stdexec::set_value_t) && noexcept;

    void set_stopped(stdexec::set_stopped_t) && noexcept;

    stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept;
  };

  template <class SenderFactory, class Buffer, class Receiver>
  struct buffered_sequence_op : buffered_sequence_op_base<SenderFactory, Buffer> {
    Receiver receiver_;
    std::optional<stdexec::connect_result_t<
      exec::next_sender_of_t<Receiver, buffered_item<SenderFactory, Buffer>>,
      buffered_next_receiver<SenderFactory, Buffer, Receiver>>>
      next_op_;

    explicit buffered_sequence_op(
      Receiver receiver,
      SenderFactory sndr,
      Buffer buffer,
      ::off_t offset)
      : buffered_sequence_op_base<
        SenderFactory,
        Buffer>{std::move(sndr), buffer_span<Buffer>{buffer, offset}}
      , receiver_(std::move(receiver)) {
      connect_next();
    }

    decltype(auto) connect_next() {
      return next_op_.emplace(stdexec::__conv{[this] {
        return stdexec::connect(
          exec::set_next(receiver_, buffered_item<SenderFactory, Buffer>{this}),
          buffered_next_receiver<SenderFactory, Buffer, Receiver>{this});
      }});
    }

    void start(stdexec::start_t) noexcept {
      stdexec::start(*next_op_);
    }
  };

  template <class SenderFactory, class Buffer>
  struct buffered_sequence {
    using is_sender = exec::sequence_tag;

    SenderFactory sender_;
    Buffer buffer_;
    ::off_t offset_;

    explicit buffered_sequence(SenderFactory sndr, Buffer buffer, ::off_t offset = -1) noexcept
      : sender_{std::move(sndr)}
      , buffer_{buffer}
      , offset_{offset} {
    }

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::exception_ptr),
      stdexec::set_stopped_t()>;

    using item_types = exec::item_types<buffered_item<SenderFactory, Buffer>>;

    template <exec::sequence_receiver_of<item_types> Receiver>
    buffered_sequence_op<SenderFactory, Buffer, Receiver>
      subscribe(exec::subscribe_t, Receiver rcvr) const
      noexcept(nothrow_move_constructible<Receiver>) {
      return buffered_sequence_op<SenderFactory, Buffer, Receiver>{
        static_cast<Receiver&&>(rcvr), sender_, buffer_, offset_};
    }

    auto get_sequence_env(exec::get_sequence_env_t) const noexcept {
      return exec::make_env(exec::with(exec::parallelism, exec::lock_step));
    }
  };

  template <class SenderFactory, class Buffer, class Receiver>
  void buffered_next_receiver<SenderFactory, Buffer, Receiver>::set_value(
    stdexec::set_value_t) && noexcept {
    if (op_->buffer_.data().empty()) {
      stdexec::set_value(static_cast<Receiver&&>(op_->receiver_));
      return;
    }
    try {
      stdexec::start(op_->connect_next());
    } catch (...) {
      stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), std::current_exception());
    }
  }

  template <class SenderFactory, class Buffer, class Receiver>
  void buffered_next_receiver<SenderFactory, Buffer, Receiver>::set_stopped(
    stdexec::set_stopped_t) && noexcept {
    exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->receiver_));
  }

  template <class SenderFactory, class Buffer, class Receiver>
  stdexec::env_of_t<Receiver> buffered_next_receiver<SenderFactory, Buffer, Receiver>::get_env(
    stdexec::get_env_t) const noexcept {
    return stdexec::get_env(op_->receiver_);
  }
}