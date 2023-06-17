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

#include "./file_handle.hpp"

#include "../net_concepts.hpp"
#include "../net/ip/endpoint.hpp"

namespace sio::io_uring {
  struct socket_handle;

  namespace socket_ {
    template <class Protocol, class Receiver>
    struct operation_base {
      exec::io_uring_context& context_;
      Protocol protocol_;
      [[no_unique_address]] Receiver receiver_;

      operation_base(
        exec::io_uring_context& context,
        Protocol protocol,
        Receiver receiver) noexcept(nothrow_move_constructible<Receiver>)
        : context_{context}
        , protocol_{protocol}
        , receiver_{static_cast<Receiver&&>(receiver)} {
      }

      exec::io_uring_context& context() const noexcept {
        return context_;
      }

      static std::true_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe&) noexcept {}

      void complete(const ::io_uring_cqe&) noexcept;
    };

    template <class Protocol, class Receiver>
    using operation = io_task_facade<operation_base<Protocol, Receiver>>;

    template <class Protocol>
    struct sender {
      using is_sender = void;

      exec::io_uring_context* context_;
      Protocol protocol_;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(socket_handle),
        stdexec::set_error_t(std::error_code)>;

      template <class Receiver>
      auto
        connect(stdexec::connect_t, Receiver rcvr) const noexcept(nothrow_move_constructible<Receiver>) {
        return operation<Protocol, Receiver>{std::in_place, *context_, protocol_, static_cast<Receiver&&>(rcvr)};
      }
 
      env get_env(stdexec::get_env_t) const noexcept {
        return {context_->get_scheduler()};
      }
    };
  }

  namespace connect_ {
    struct submission {
      int fd_;
      ip::endpoint peer_endpoint_;

      static std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& sqe) noexcept;
    };

    template <class Receiver>
    struct operation_base
      : stoppable_op_base<Receiver>
      , submission {
      operation_base(
        int fd,
        ip::endpoint peer_endpoint,
        exec::io_uring_context& context,
        Receiver rcvr) noexcept
        : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(rcvr)}
        , submission{fd, peer_endpoint} {
      }

      void complete(const ::io_uring_cqe& cqe) noexcept {
        if (cqe.res == 0) {
          stdexec::set_value(static_cast<Receiver&&>(this->receiver_));
        } else {
          stdexec::set_error(
            static_cast<Receiver&&>(this->receiver_),
            std::error_code(-cqe.res, std::system_category()));
        }
      }
    };

    template <class Receiver>
    using operation = stoppable_task_facade<operation_base<Receiver>>;

    struct sender {
      using is_sender = void;

      exec::io_uring_context* context_;
      ip::endpoint peer_endpoint_;
      int fd_;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::error_code),
        stdexec::set_stopped_t()>;

      template <stdexec::receiver_of<completion_signatures> Receiver>
      operation<Receiver> connect(stdexec::connect_t, Receiver rcvr) const
        noexcept(nothrow_move_constructible<Receiver>) {
        return operation<Receiver>{
          std::in_place, fd_, peer_endpoint_, context_, static_cast<Receiver&&>(rcvr)};
      }

      env get_env(stdexec::get_env_t) const noexcept {
        return {context_->get_scheduler()};
      }
    };
  }

  struct socket_handle : byte_stream {
    connect_::sender connect(async::connect_t, ip::endpoint peer_endpoint) noexcept {
      return {this->context_, peer_endpoint, fd_};
    }

    ip::endpoint local_endpoint() const;
    ip::endpoint remote_endpoint() const;
  };

  template <class Protocol>
  struct socket_resource {
    exec::io_uring_context& context_;
    Protocol protocol_;

    explicit socket_resource(exec::io_uring_context& context, Protocol protocol) noexcept
      : context_{context}
      , protocol_{protocol} {
    }

    explicit socket_resource(exec::io_uring_context* context, Protocol protocol) noexcept
      : context_{*context}
      , protocol_{protocol} {
    }

    socket_::sender<Protocol> open(async::open_t) noexcept {
      return {&context_, protocol_};
    }
  };

  namespace socket_ {
    template <class Protocol, class Receiver>
    void operation_base<Protocol, Receiver>::complete(const ::io_uring_cqe&) noexcept {
      int rc = ::socket(protocol_.family(), protocol_.type(), protocol_.protocol());
      if (rc == -1) {
        stdexec::set_error(
          static_cast<Receiver&&>(receiver_), std::error_code(errno, std::system_category()));
      } else {
        stdexec::set_value(
          static_cast<Receiver&&>(receiver_),
          socket_handle{
            byte_stream{native_fd_handle{context_, rc}}
        });
      }
    }
  }
}