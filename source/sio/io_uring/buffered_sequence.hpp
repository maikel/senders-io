#pragma once

#include "../concepts.hpp"
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
      try {
        stdexec::start(op_->connect_next());
      } catch (...) {
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), std::current_exception());
      }
    }

    void set_stopped(stdexec::set_stopped_t) && noexcept {
      stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
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
    std::optional<stdexec::connect_result_t<
      exec::next_sender_of_t<Receiver, buffered_item<Sender, Receiver>>,
      buffered_next_receiver<Sender, Receiver>>>
      next_op_;

    explicit buffered_sequence_op(Receiver receiver, Sender sndr)
      : receiver_(static_cast<Receiver&&>(receiver))
      , sender_(sndr) {
      connect_next();
    }

    decltype(auto) connect_next() {
      return next_op_.emplace(stdexec::__conv{[this] {
        return stdexec::connect(
          exec::set_next(receiver_, buffered_item<Sender, Receiver>{this}),
          buffered_next_receiver<Sender, Receiver>{this});
      }});
    }

    void start(stdexec::start_t) noexcept {
      stdexec::start(*next_op_);
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

    auto get_sequence_env(exec::get_sequence_env_t) const noexcept {
      return exec::make_env(exec::with(exec::parallelism, exec::lock_step));
    }
  };
}