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

namespace sio::io_uring {
  template <class Context, class Protocol>
  struct basic_socket_handle;

  namespace socket_ {
    template <class Context, class Protocol, class Receiver>
    struct operation_base {
      Context& context_;
      Protocol protocol_;
      [[no_unique_address]] Receiver receiver_;

      operation_base(Context& context, Protocol protocol, Receiver receiver) noexcept(
        nothrow_move_constructible<Receiver>)
        : context_{context}
        , protocol_{protocol}
        , receiver_{static_cast<Receiver&&>(receiver)} {
      }

      Context& context() const noexcept {
        return context_;
      }

      static std::true_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe&) noexcept {
      }

      void complete(const ::io_uring_cqe&) noexcept;
    };

    template <class Context, class Protocol, class Receiver>
    using operation = io_task_facade<operation_base<Context, Protocol, Receiver>>;

    template <class Context, class Protocol>
    struct sender {
      using is_sender = void;

      Context* context_;
      Protocol protocol_;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(basic_socket_handle<Context, Protocol>),
        stdexec::set_error_t(std::error_code)>;

      template <class Receiver>
      auto connect(stdexec::connect_t, Receiver rcvr) const
        noexcept(nothrow_move_constructible<Receiver>) {
        return operation<Context, Protocol, Receiver>{
          std::in_place, *context_, protocol_, static_cast<Receiver&&>(rcvr)};
      }

      env<Context> get_env(stdexec::get_env_t) const noexcept {
        return {context_};
      }
    };
  }

  namespace connect_ {

    template <class Context, class Protocol, class Receiver>
    struct operation_base : stoppable_op_base<Context, Receiver> {
      int fd_;
      typename Protocol::endpoint peer_endpoint_;

      operation_base(
        int fd,
        typename Protocol::endpoint peer_endpoint,
        Context& context,
        Receiver rcvr) noexcept
        : stoppable_op_base<Context, Receiver>{context, static_cast<Receiver&&>(rcvr)}
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

    template <class Context, class Protocol, class Receiver>
    using operation = stoppable_task_facade_t<operation_base<Context, Protocol, Receiver>>;

    template <class Context, class Protocol>
    struct sender {
      using is_sender = void;

      Context* context_;
      typename Protocol::endpoint peer_endpoint_;
      int fd_;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::error_code),
        stdexec::set_stopped_t()>;

      template <stdexec::receiver_of<completion_signatures> Receiver>
      operation<Context, Protocol, Receiver> connect(stdexec::connect_t, Receiver rcvr) const
        noexcept(nothrow_move_constructible<Receiver>) {
        return operation<Context, Protocol, Receiver>{
          std::in_place, fd_, peer_endpoint_, *context_, static_cast<Receiver&&>(rcvr)};
      }

      env<Context> get_env(stdexec::get_env_t) const noexcept {
        return {context_};
      }
    };
  }

  template <class Context, class Receiver>
  struct sendmsg_operation_base : stoppable_op_base<Context, Receiver> {
    int fd_;
    ::msghdr msg_;

    sendmsg_operation_base(Receiver rcvr, Context* context, int fd, ::msghdr msg)
      : stoppable_op_base<Context, Receiver>{*context, static_cast<Receiver&&>(rcvr)}
      , fd_{fd}
      , msg_{msg} {
    }

    static std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_SENDMSG;
      sqe_.fd = fd_;
      sqe_.addr = std::bit_cast<__u64>(&msg_);
      sqe = sqe_;
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(static_cast<sendmsg_operation_base&&>(*this).receiver(), cqe.res);
      } else {
        stdexec::set_error(
          static_cast<sendmsg_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Context, class Receiver>
  using sendmsg_operation = stoppable_task_facade_t<sendmsg_operation_base<Context, Receiver>>;

  template <class Context>
  struct sendmsg_sender {
    using is_sender = void;
    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    template <class Receiver>
    sendmsg_operation<Context, Receiver> connect(stdexec::connect_t, Receiver rcvr) const
      noexcept(nothrow_move_constructible<Receiver>) {
      return sendmsg_operation<Context, Receiver>{
        std::in_place, static_cast<Receiver&&>(rcvr), context_, fd_, msg_};
    }

    Context* context_;
    int fd_;
    ::msghdr msg_;
  };

  template <class Context, class Protocol>
  struct basic_socket_handle : basic_byte_stream<Context> {
    basic_socket_handle() = default;

    basic_socket_handle(Context& context, int fd, Protocol proto) noexcept
      : byte_stream{context, fd}
      , protocol_{proto} {
    }

    using endpoint = typename Protocol::endpoint;

    [[no_unique_address]] Protocol protocol_;

    connect_::sender<Context, Protocol>
      connect(async::connect_t, endpoint peer_endpoint) const noexcept {
      return {this->context_, peer_endpoint, this->fd_};
    }

    sendmsg_sender<Context> sendmsg(async::sendmsg_t, ::msghdr msg) const noexcept {
      return {this->context_, this->fd_, msg};
    }

    void bind(endpoint local_endpoint) const {
      if (::bind(this->fd_, (sockaddr*) local_endpoint.data(), local_endpoint.size()) == -1) {
        throw std::system_error(errno, std::system_category());
      }
    }

    endpoint local_endpoint() const;
    endpoint remote_endpoint() const;
  };

  template <class Protocol>
  using socket_handle = basic_socket_handle<exec::io_uring_context, Protocol>;

  template <class Context, class Protocol>
  struct basic_socket {
    Context& context_;
    Protocol protocol_;

    explicit basic_socket(Context& context, Protocol protocol = Protocol()) noexcept
      : context_{context}
      , protocol_{protocol} {
    }

    explicit basic_socket(Context* context, Protocol protocol = Protocol()) noexcept
      : context_{*context}
      , protocol_{protocol} {
    }

    socket_::sender<Context, Protocol> open(async::open_t) noexcept {
      return {&context_, protocol_};
    }
  };

  template <class Context, class Protocol>
  basic_socket(Context&, Protocol) -> basic_socket<Context, Protocol>;

  template <class Context, class Protocol>
  basic_socket(Context*, Protocol) -> basic_socket<Context, Protocol>;

  template <class Protocol>
  using socket = basic_socket<exec::io_uring_context, Protocol>;

  namespace socket_ {
    template <class Context, class Protocol, class Receiver>
    void operation_base<Context, Protocol, Receiver>::complete(const ::io_uring_cqe&) noexcept {
      int rc = ::socket(protocol_.family(), protocol_.type(), protocol_.protocol());
      if (rc == -1) {
        stdexec::set_error(
          static_cast<Receiver&&>(receiver_), std::error_code(errno, std::system_category()));
      } else {
        stdexec::set_value(
          static_cast<Receiver&&>(receiver_), basic_socket_handle{context_, rc, protocol_});
      }
    }
  }

  template <class Context, class Protocol, class Receiver>
  struct accept_operation_base : stoppable_op_base<Context, Receiver> {
    int fd_;
    [[no_unique_address]] Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;
    socklen_t addrlen_{};

    accept_operation_base(
      Context& context,
      Receiver receiver,
      int fd,
      Protocol protocol,
      typename Protocol::endpoint local_endpoint) noexcept
      : stoppable_op_base<Context, Receiver>{context, static_cast<Receiver&&>(receiver)}
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

  template <class Context, class Protocol, class Receiver>
  using accept_operation =
    stoppable_task_facade_t<accept_operation_base<Context, Protocol, Receiver>>;

  template <class Context, class Protocol>
  struct accept_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_handle<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    int fd_;
    Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;

    template <stdexec::receiver_of<completion_signatures> Receiver>
    accept_operation<Context, Protocol, Receiver>
      connect(stdexec::connect_t, Receiver rcvr) noexcept(nothrow_decay_copyable<Receiver>) {
      return accept_operation<Context, Protocol, Receiver>{
        std::in_place,
        *context_,
        static_cast<Receiver&&>(rcvr),
        fd_,
        protocol_,
        static_cast<typename Protocol::endpoint&&>(local_endpoint_)};
    }
  };

  template <class Context, class Protocol>
  struct basic_acceptor_handle : basic_native_fd_handle<Context> {
    Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;

    basic_acceptor_handle(
      Context& context,
      int fd,
      Protocol protocol,
      const typename Protocol::endpoint& local_endpoint) noexcept
      : basic_native_fd_handle<Context>(context, fd)
      , protocol_(protocol)
      , local_endpoint_(local_endpoint) {
    }

    accept_sender<Context, Protocol> accept_once(async::accept_once_t) const noexcept {
      return accept_sender<Context, Protocol>{
        this->context_, this->fd_, protocol_, local_endpoint_};
    }
  };

  template <class Protocol>
  using acceptor_handle = basic_acceptor_handle<exec::io_uring_context, Protocol>;

  template <class Context, class Protocol>
  struct basic_acceptor {
    Context& context_;
    Protocol protocol_;
    typename Protocol::endpoint local_endpoint_;

    explicit basic_acceptor(
      Context& context,
      Protocol protocol,
      typename Protocol::endpoint ep) noexcept
      : context_{context}
      , protocol_{protocol}
      , local_endpoint_(ep) {
    }

    explicit basic_acceptor(
      Context* context,
      Protocol protocol,
      typename Protocol::endpoint ep) noexcept
      : context_{*context}
      , protocol_{protocol}
      , local_endpoint_(ep) {
    }

    static void throw_on_error(int rc) {
      std::error_code ec{rc, std::system_category()};
      if (ec) {
        throw std::system_error(ec);
      }
    }

    auto open(async::open_t) noexcept {
      return stdexec::then(
        basic_socket<Context, Protocol>{context_, protocol_}.open(async::open),
        [*this](basic_socket_handle<Context, Protocol> handle) {
          int one = 1;
          throw_on_error(::setsockopt(
            handle.fd_, SOL_SOCKET, SO_REUSEADDR, &one, static_cast<socklen_t>(sizeof(int))));
          throw_on_error(::bind(handle.fd_, local_endpoint_.data(), local_endpoint_.size()));
          throw_on_error(::listen(handle.fd_, 16));
          return acceptor_handle<Protocol>{context_, handle.get(), protocol_, local_endpoint_};
        });
    }
  };

  template <class Context, class Protocol>
  basic_acceptor(Context&, Protocol, typename Protocol::endpoint)
    -> basic_acceptor<Context, Protocol>;

  template <class Protocol>
  using acceptor = basic_acceptor<exec::io_uring_context, Protocol>;
}