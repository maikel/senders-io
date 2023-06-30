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
#include "../deferred.hpp"

namespace sio::io_uring {
  template <class Protocol>
  struct socket_handle;

  namespace socket_ {
    template <class Protocol, class Receiver>
    struct operation_base {
      exec::io_uring_context& context_;
      Protocol protocol_;
      int* fd_;
      [[no_unique_address]] Receiver receiver_;

      operation_base(
        exec::io_uring_context& context,
        Protocol protocol,
        int* fd,
        Receiver receiver) noexcept(nothrow_move_constructible<Receiver>)
        : context_{context}
        , protocol_{protocol}
        , fd_{fd}
        , receiver_{static_cast<Receiver&&>(receiver)} {
      }

      exec::io_uring_context& context() const noexcept {
        return context_;
      }

      static std::true_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe&) noexcept {
      }

      void complete(const ::io_uring_cqe&) noexcept;
    };

    template <class Protocol, class Receiver>
    using operation = io_task_facade<operation_base<Protocol, Receiver>>;

    template <class Protocol>
    struct sender {
      using is_sender = void;

      exec::io_uring_context* context_;
      Protocol protocol_;
      int* fd_;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(socket_handle<Protocol>),
        stdexec::set_error_t(std::error_code)>;

      template <class Receiver>
      auto connect(stdexec::connect_t, Receiver rcvr) const
        noexcept(nothrow_move_constructible<Receiver>) {
        return operation<Protocol, Receiver>{
          std::in_place, *context_, protocol_, fd_, static_cast<Receiver&&>(rcvr)};
      }

      env get_env(stdexec::get_env_t) const noexcept {
        return {context_->get_scheduler()};
      }
    };
  }

  namespace connect_ {

    template <class Protocol, class Receiver>
    struct operation_base : stoppable_op_base<Receiver> {
      int fd_;
      typename Protocol::endpoint peer_endpoint_;

      operation_base(
        int fd,
        typename Protocol::endpoint peer_endpoint,
        exec::io_uring_context& context,
        Receiver rcvr) noexcept
        : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(rcvr)}
        , fd_{fd}
        , peer_endpoint_{peer_endpoint} {
      }

      static std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& sqe) noexcept {
        ::io_uring_sqe sqe_{};
        sqe_.opcode = IORING_OP_CONNECT;
        sqe_.fd = fd_;
        sqe_.addr = std::bit_cast<__u64>(peer_endpoint_.data());
        sqe_.off = peer_endpoint_.size();
        sqe = sqe_;
      }

      void complete(const ::io_uring_cqe& cqe) noexcept {
        if (cqe.res == 0) {
          stdexec::set_value(static_cast<operation_base&&>(*this).receiver());
        } else {
          stdexec::set_error(
            static_cast<operation_base&&>(*this).receiver(),
            std::error_code(-cqe.res, std::system_category()));
        }
      }
    };

    template <class Protocol, class Receiver>
    using operation = stoppable_task_facade<operation_base<Protocol, Receiver>>;

    template <class Protocol>
    struct sender {
      using is_sender = void;

      exec::io_uring_context* context_;
      typename Protocol::endpoint peer_endpoint_;
      int fd_;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::error_code),
        stdexec::set_stopped_t()>;

      template <stdexec::receiver_of<completion_signatures> Receiver>
      operation<Protocol, Receiver> connect(stdexec::connect_t, Receiver rcvr) const
        noexcept(nothrow_move_constructible<Receiver>) {
        return operation<Protocol, Receiver>{
          std::in_place, fd_, peer_endpoint_, *context_, static_cast<Receiver&&>(rcvr)};
      }

      env get_env(stdexec::get_env_t) const noexcept {
        return {context_->get_scheduler()};
      }
    };
  }

  template <class Protocol>
  struct socket_handle : byte_stream {
    socket_handle(exec::io_uring_context& context, int fd, Protocol proto) noexcept
      : byte_stream{context, fd}
      , protocol_{proto} {
    }

    using endpoint = typename Protocol::endpoint;

    [[no_unique_address]] Protocol protocol_;

    connect_::sender<Protocol> connect(async::connect_t, endpoint peer_endpoint) const noexcept {
      return {this->context_, peer_endpoint, fd_};
    }

    endpoint local_endpoint() const;
    endpoint remote_endpoint() const;
  };

  template <class Protocol>
  struct socket {
    exec::io_uring_context& context_;
    Protocol protocol_;
    int fd_{-1};

    explicit socket(exec::io_uring_context& context, Protocol protocol) noexcept
      : context_{context}
      , protocol_{protocol} {
    }

    explicit socket(exec::io_uring_context* context, Protocol protocol) noexcept
      : context_{*context}
      , protocol_{protocol} {
    }

    socket_::sender<Protocol> open(async::open_t) noexcept {
      return {&context_, protocol_, &fd_};
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
        *fd_ = rc;
        stdexec::set_value(
          static_cast<Receiver&&>(receiver_), socket_handle{context_, rc, protocol_});
      }
    }
  }

  template <class Protocol, class Receiver>
  struct accept_operation_base : stoppable_op_base<Receiver> {
    int fd_;
    [[no_unique_address]] Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;
    socklen_t addrlen_{};

    accept_operation_base(
      exec::io_uring_context& context,
      Receiver receiver,
      int fd,
      Protocol protocol,
      typename Protocol::endpoint local_endpoint) noexcept
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , fd_{fd}
      , protocol_(protocol)
      , local_endpoint_(static_cast<typename Protocol::endpoint&&>(local_endpoint))
      , addrlen_(local_endpoint_.size()) {
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_ACCEPT;
      sqe_.fd = fd_;
      sqe_.addr = std::bit_cast<__u64>(local_endpoint_.data());
      sqe_.addr2 = std::bit_cast<__u64>(&addrlen_);
      sqe = sqe_;
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<accept_operation_base&&>(*this).receiver(),
          socket_handle<Protocol>{this->context(), cqe.res, protocol_});
      } else {
        SIO_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<accept_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Protocol, class Receiver>
  using accept_operation = stoppable_task_facade<accept_operation_base<Protocol, Receiver>>;

  template <class Protocol>
  struct accept_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_handle<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    int fd_;
    Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;

    template <stdexec::receiver_of<completion_signatures> Receiver>
    accept_operation<Protocol, Receiver>
      connect(stdexec::connect_t, Receiver rcvr) noexcept(nothrow_decay_copyable<Receiver>) {
      return accept_operation<Protocol, Receiver>{
        std::in_place,
        *context_,
        static_cast<Receiver&&>(rcvr),
        fd_,
        protocol_,
        static_cast<typename Protocol::endpoint&&>(local_endpoint_)};
    }
  };

  template <class Protocol>
  struct acceptor_handle : native_fd_handle {
    Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;

    acceptor_handle(
      exec::io_uring_context& context,
      int fd,
      Protocol protocol,
      const typename Protocol::endpoint& local_endpoint) noexcept
      : native_fd_handle(context, fd)
      , protocol_(protocol)
      , local_endpoint_(local_endpoint) {
    }

    accept_sender<Protocol> accept_once(async::accept_once_t) const noexcept {
      return accept_sender<Protocol>{context_, fd_, protocol_, local_endpoint_};
    }
  };

  template <class Protocol>
  struct acceptor_resource {
    exec::io_uring_context& context_;
    Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;

    explicit acceptor_resource(
      exec::io_uring_context& context,
      Protocol protocol,
      typename Protocol::endpoint ep) noexcept
      : context_{context}
      , protocol_{protocol}
      , local_endpoint_(ep) {
    }

    explicit acceptor_resource(
      exec::io_uring_context* context,
      Protocol protocol,
      typename Protocol::endpoint ep) noexcept
      : context_{*context}
      , protocol_{protocol}
      , local_endpoint_(ep) {
    }

    void throw_on_error(int rc) {
      std::error_code ec{rc, std::system_category()};
      if (ec) {
        throw std::system_error(ec);
      }
    }

    auto open(async::open_t) noexcept {
      return stdexec::then(
        socket<Protocol>{context_, protocol_}.open(async::open),
        [this](socket_handle<Protocol> handle) {
          int one = 1;
          throw_on_error(::setsockopt(
            handle.fd_, SOL_SOCKET, SO_REUSEADDR, &one, static_cast<socklen_t>(sizeof(int))));
          throw_on_error(::bind(handle.fd_, local_endpoint_.data(), local_endpoint_.size()));
          throw_on_error(::listen(handle.fd_, 16));
          return acceptor_handle<Protocol>{context_, handle.get(), protocol_, local_endpoint_};
        });
    }
  };

  template <class Proto>
  auto make_deferred_socket(exec::io_uring_context* ctx, Proto proto) {
    return make_deferred<socket<Proto>>(ctx, proto);
  }

  template <class Proto>
  auto
    make_deferred_acceptor(exec::io_uring_context* ctx, Proto proto, typename Proto::endpoint ep) {
    return make_deferred<acceptor_resource<Proto>>(ctx, proto, ep);
  }

}