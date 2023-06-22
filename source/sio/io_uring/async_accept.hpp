/*
 * Copyright (c) 2023 Xiaoming Zhang
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

#include "sio/concepts.hpp"
#include "sio/sequence/sequence_concepts.hpp"
#include "sio/io_uring/file_handle.hpp"
#include "sio/io_uring/socket_handle.hpp"
#include "sio/net/ip/address.hpp"
#include "sio/async_allocator.hpp"

#include <exec/env.hpp>
#include <exec/linux/io_uring_context.hpp>
#include <exec/trampoline_scheduler.hpp>

#include <sys/socket.h>

namespace sio::io_uring {
  namespace accept_ {
    using namespace stdexec;

    template <class Receiver>
    struct operation_base;

    template <class Receiver>
    struct next_receiver {
      using is_receiver = void;

      operation_base<Receiver>* op_;
      void* client_op_;

      void complete() noexcept {
        operation_base<Receiver>* op = op_;
        op->destroy(client_op_);
        op->deallocate(client_op_);
      }

      void set_value(stdexec::set_value_t) && noexcept {
        complete();
      }

      void set_error(stdexec::set_error_t, std::error_code error) && noexcept {
        op_->notify_error(error);
        complete();
      }

      void set_error(stdexec::set_error_t, std::exception_ptr error) && noexcept {
        op_->notify_error(static_cast<std::exception_ptr&&>(error));
        complete();
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->notify_stopped();
        complete();
      }

      env_of_t<Receiver> get_env(get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }
    };

    template <class Receiver>
    struct operation_next {
      Receiver rcvr_;
      io_uring::acceptor acceptor_;

      operation_next(Receiver rcvr, io_uring::acceptor acceptor)
        : rcvr_(static_cast<Receiver&&>(rcvr))
        , acceptor_(static_cast<io_uring::acceptor&&>(acceptor)) {
      }

      virtual void start_next() noexcept = 0;
    };

    template <class Receiver>
    auto next_accept_sender(operation_next<Receiver>& op) {
      return stdexec::let_value(
        accept_sender{op.acceptor_.context_, op.acceptor_.fd_, op.acceptor_.local_endpoint_},
        [&op](socket_handle fd) {
          return exec::finally(
            exec::set_next(
              op.rcvr_,
              stdexec::then(
                stdexec::just(fd),
                [&op, &fd](socket_handle client) noexcept {
                  op.start_next();
                  return fd = client;
                })),
            async::close(fd));
        });
    }

    template <class Receiver>
    struct operation_base : operation_next<Receiver> {

      std::atomic<std::size_t> ref_count_{};
      std::atomic<bool> stopped_{false};
      std::variant<std::monostate, std::exception_ptr, std::error_code> error_{};

      operation_base(Receiver rcvr, io_uring::acceptor acceptor) noexcept
        : operation_next<Receiver>{
          static_cast<Receiver&&>(rcvr),
          static_cast<io_uring::acceptor&&>(acceptor)} {
      }

      using next_accept_sender_of_t =
        decltype(next_accept_sender(*(operation_next<Receiver>*) nullptr));

      using next_operation =
        stdexec::connect_result_t<next_accept_sender_of_t, next_receiver<Receiver>>;

      void increase_ref() noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
      }

      bool decrease_ref() noexcept {
        return ref_count_.fetch_sub(1, std::memory_order_relaxed) == 1;
      }

      auto get_allocator() const noexcept {
        using Alloc = decltype(sio::async::get_allocator(stdexec::get_env(this->rcvr_)));
        using NextAlloc = std::allocator_traits<Alloc>::template rebind_alloc<next_operation>;
        return NextAlloc(sio::async::get_allocator(stdexec::get_env(this->rcvr_)));
      }

      void deallocate(void* op) noexcept {
        SIO_ASSERT(op != nullptr);
        stdexec::sync_wait(
          sio::async::deallocate(get_allocator(), static_cast<next_operation*>(op)));
        if (decrease_ref()) {
          exec::set_value_unless_stopped(static_cast<Receiver&&>(this->rcvr_));
        }
      }

      template <class Error>
      void notify_error(Error error) noexcept {
        if (!stopped_.exchange(true, std::memory_order_relaxed)) {
          error_ = error;
        }
      }

      void notify_stopped() noexcept {
        stopped_.store(true, std::memory_order_relaxed);
      }

      void destroy(void* op) {
        std::destroy_at(static_cast<next_operation*>(op));
      }
    };

    template <class Receiver>
    struct operation;

    template <class Receiver>
    struct allocation_receiver {
      operation<Receiver>* op_;

      using next_operation = stdexec::connect_result_t<
        typename operation_base<Receiver>::next_accept_sender_of_t,
        next_receiver<Receiver>>;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }

      void set_value(stdexec::set_value_t, next_operation* client_op) && noexcept {
        try {
          if (op_->stopped_.load(std::memory_order_relaxed)) {
            op_->deallocate(client_op);
            return;
          }
          std::construct_at(client_op, stdexec::__conv{[&] {
                              return stdexec::connect(
                                next_accept_sender(*op_), next_receiver<Receiver>{op_, client_op});
                            }});
          stdexec::start(*client_op);
        } catch (...) {
          op_->notify_error(std::current_exception());
          op_->deallocate(client_op);
        }
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& err) && noexcept {
        op_->notify_error(static_cast<Error&&>(err));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->notify_stopped();
      }
    };

    template <class Receiver>
    struct operation : operation_base<Receiver> {
      using next_allocator_t =
        decltype(std::declval<const operation_base<Receiver>&>().get_allocator());
      using allocate_t = decltype(sio::async::allocate(std::declval<next_allocator_t>(), 0));

      operation(Receiver rcvr, io_uring::acceptor acceptor)
        : operation_base<Receiver>{
          static_cast<Receiver&&>(rcvr),
          static_cast<io_uring::acceptor&&>(acceptor)} {
      }

      std::optional<stdexec::connect_result_t<allocate_t, allocation_receiver<Receiver>>> op_{};

      void start_next() noexcept override {
        try {
          this->increase_ref();
          stdexec::start(op_.emplace(__conv{[&] {
            auto alloc = this->get_allocator();
            return stdexec::connect(
              sio::async::allocate(alloc, 1), allocation_receiver<Receiver>{this});
          }}));
        } catch (...) {
          this->notify_error(std::current_exception());
          if (this->decrease_ref()) {
            exec::set_value_unless_stopped(static_cast<Receiver&&>(this->rcvr_));
          }
        }
      }

      void start(start_t) noexcept {
        start_next();
      }
    };

    struct sequence {
      using is_sender = exec::sequence_tag;

      using completion_signatures = stdexec::completion_signatures<
        set_value_t(sio::io_uring::socket_handle),
        set_error_t(std::error_code),
        set_error_t(std::exception_ptr),
        set_stopped_t()>;

      io_uring::acceptor acceptor_;

      // !!!! The commented lines cause compilation errors.
      template <
        decays_to<sequence> Self,
        exec::sequence_receiver_of<completion_signatures> Receiver>
      static operation<Receiver> subscribe(Self&& self, exec::subscribe_t, Receiver rcvr) //
        noexcept(nothrow_decay_copyable<Receiver>) {
        return {static_cast<Receiver&&>(rcvr), self.acceptor_};
      }
    };

    struct async_accept_t {
      sequence operator()(io_uring::acceptor& acceptor) const noexcept {
        return {acceptor};
      }
    };
  }

  using accept_::async_accept_t;
  inline constexpr async_accept_t async_accept;
}