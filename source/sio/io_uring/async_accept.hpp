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
#include "sio/net/ip/address.hpp"

#include <exec/env.hpp>
#include <exec/linux/io_uring_context.hpp>
#include <exec/trampoline_scheduler.hpp>
#include <sys/socket.h>

namespace sio {
  namespace accept_ {
    using namespace stdexec;

    // TODO: with async_allocator
    // using sender_t = async_allocator(sender_t);
    using sender_t = io_uring::accept_sender;

    template <class Receiver>
    struct operation;

    template <class Receiver>
    struct next_receiver {
      using is_receiver = void;

      operation<Receiver>* op_;

      void set_value(set_value_t) && noexcept {
        op_->start_next();
      }

      void set_stopped(set_stopped_t) && noexcept {
        if constexpr (unstoppable_token<stop_token_of_t<env_of_t<Receiver>>>) {
          stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
        } else {
          auto token = stdexec::get_stop_token(stdexec::get_env(op_->rcvr_));
          if (token.stop_requested()) {
            stdexec::set_stopped(static_cast<Receiver&&>(op_->rcvr_));
          } else {
            stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
          }
        }
      }

      env_of_t<Receiver> get_env(get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }
    };

    template <class Receiver>
    struct operation {
      Receiver rcvr_;
      io_uring::acceptor& acceptor_;

      using item_sender = decltype(stdexec::on(
        std::declval<exec::trampoline_scheduler&>(),
        std::declval<sender_t>()));

      // !!!! The commented lines cause compilation errors.
      // std::optional< connect_result_t<
      //   exec::__next_sender_of_t<Receiver, item_sender>, //
      //   next_receiver<Receiver>>>
      //   op_{};
      exec::trampoline_scheduler scheduler_{};

      void start_next() noexcept {
        // try {
        //   stdexec::start(op_.emplace(__conv{[&] {
        //     return stdexec::connect(
        //       exec::set_next(
        //         rcvr_,
        //         stdexec::on(
        //           scheduler_,
        //           sender_t{acceptor_.context_, acceptor_.fd_, acceptor_.local_endpoint_})),
        //       next_receiver<Receiver>{this});
        //   }}));
        // } catch (...) {
        //   stdexec::set_error(static_cast<Receiver&&>(rcvr_), std::current_exception());
        // }
      }

      void start(start_t) noexcept {
        start_next();
      }
    };

    struct sequence {
      using is_sender = exec::sequence_tag;

      using completion_signatures = stdexec::completion_signatures<
        set_value_t(sio::io_uring::byte_stream&&),
        set_error_t(std::exception_ptr),
        set_stopped_t()>;

      io_uring::acceptor& acceptor_;

      // !!!! The commented lines cause compilation errors.
      template <
        __decays_to<sequence> Self,
        exec::sequence_receiver_of<completion_signatures> Receiver>
      // requires sender_to< exec::__next_sender_of_t<Receiver, sender_t>, next_receiver<Receiver>>
      static operation<Receiver> subscribe(Self&& self, exec::subscribe_t, Receiver rcvr) //
        noexcept(nothrow_decay_copyable<Receiver>) {
        return {static_cast<Receiver&&>(rcvr), self.acceptor_};
      }

      auto get_sequence_env(exec::get_sequence_env_t) const noexcept {
        return exec::make_env(exec::with(exec::parallelism, exec::lock_step));
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