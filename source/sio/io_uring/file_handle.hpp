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

#include "../assert.hpp"
#include "../io_concepts.hpp"

#include "./buffered_sequence.hpp"
#include "../sequence/reduce.hpp"

#include "exec/linux/io_uring_context.hpp"
#include "sio/concepts.hpp"
#include "sio/net/ip/endpoint.hpp"

#include <exception>
#include <filesystem>
#include <span>

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

namespace sio::io_uring {
  struct close_submission {
    exec::io_uring_context& context_;
    int fd_;

    close_submission(exec::io_uring_context& context, int fd) noexcept
      : context_{context}
      , fd_{fd} {
    }

    exec::io_uring_context& context() const noexcept {
      return context_;
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Receiver>
  struct close_operation_base : close_submission {
    [[no_unique_address]] Receiver receiver_;

    close_operation_base(exec::io_uring_context& context, Receiver receiver, int fd)
      : close_submission{context, fd}
      , receiver_{static_cast<Receiver&&>(receiver)} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res == 0) {
        stdexec::set_value(static_cast<Receiver&&>(receiver_));
      } else {
        SIO_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<Receiver&&>(receiver_), std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Tp>
  using io_task_facade = exec::__io_uring::__io_task_facade<Tp>;

  template <class Tp>
  using stoppable_op_base = exec::__io_uring::__stoppable_op_base<Tp>;

  template <class Tp>
  using stoppable_task_facade = exec::__io_uring::__stoppable_task_facade_t<Tp>;

  template <class Receiver>
  using close_operation = io_task_facade<close_operation_base<Receiver>>;

  struct close_sender {
    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    int fd_;

    template <class Receiver>
    close_operation<Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return close_operation<Receiver>{
        std::in_place, *context_, static_cast<Receiver&&>(rcvr), fd_};
    }
  };

  struct native_fd_handle {
    exec::io_uring_context* context_;
    int fd_;

    explicit native_fd_handle(exec::io_uring_context* context, int fd) noexcept
      : context_{context}
      , fd_{fd} {
    }

    explicit native_fd_handle(exec::io_uring_context& context, int fd) noexcept
      : context_{&context}
      , fd_{fd} {
    }

    int get() const noexcept {
      return fd_;
    }

    close_sender close(async::close_t) const noexcept {
      return {context_, fd_};
    }
  };

  struct open_data {
    std::filesystem::path path_;
    int dirfd_{0};
    int flags_{0};
    ::mode_t mode_{0};
  };

  struct open_submission {
    open_data data_;

    explicit open_submission(open_data data) noexcept;

    ~open_submission();

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Receiver>
  struct open_operation_base
    : stoppable_op_base<Receiver>
    , open_submission {

    open_operation_base(open_data data, exec::io_uring_context& context, Receiver&& receiver)
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , open_submission{static_cast<open_data&&>(data)} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<Receiver&&>(this->__receiver_), native_fd_handle{&this->context(), cqe.res});
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<Receiver&&>(this->__receiver_),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using open_operation = stoppable_task_facade<open_operation_base<Receiver>>;

  struct open_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(native_fd_handle),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    open_data data_;

    explicit open_sender(exec::io_uring_context& context, open_data data) noexcept
      : context_{&context}
      , data_{static_cast<open_data&&>(data)} {
    }

    template <decays_to<open_sender> Self, stdexec::receiver_of<completion_signatures> Receiver>
    static open_operation<Receiver>
      connect(Self&& self, stdexec::connect_t, Receiver rcvr) noexcept {
      return {
        std::in_place,
        static_cast<Self&&>(self).data_,
        *self.context_,
        static_cast<Receiver&&>(rcvr)};
    }
  };

  struct read_submission {
    std::variant<::iovec, std::span<::iovec>> buffers_;
    int fd_;
    ::off_t offset_;

    read_submission(
      std::variant<::iovec, std::span<::iovec>> buffers,
      int fd,
      ::off_t offset) noexcept;

    ~read_submission();

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Receiver>
  struct read_operation_base
    : stoppable_op_base<Receiver>
    , read_submission {
    read_operation_base(
      exec::io_uring_context& context,
      std::variant<::iovec, std::span<::iovec>> data,
      int fd,
      ::off_t offset,
      Receiver&& receiver) noexcept
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , read_submission{data, fd, offset} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(static_cast<read_operation_base&&>(*this).receiver(), cqe.res);
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<Receiver&&>(this->__receiver_),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using read_operation = stoppable_task_facade<read_operation_base<Receiver>>;

  struct read_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    std::variant<::iovec, std::span<::iovec>> buffers_;
    int fd_;
    ::off_t offset_;

    read_sender(
      exec::io_uring_context& context,
      std::span<::iovec> buffers,
      int fd,
      ::off_t offset = 0) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    read_sender(
      exec::io_uring_context& context,
      ::iovec buffers,
      int fd,
      ::off_t offset = 0) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    read_operation<Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return read_operation<Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }
  };

  struct write_submission {
    std::variant<::iovec, std::span<::iovec>> buffers_;
    int fd_;
    ::off_t offset_;

    write_submission(
      std::variant<::iovec, std::span<::iovec>> buffers,
      int fd,
      ::off_t offset) noexcept;

    ~write_submission();

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Receiver>
  struct write_operation_base
    : stoppable_op_base<Receiver>
    , write_submission {
    write_operation_base(
      exec::io_uring_context& context,
      std::variant<::iovec, std::span<::iovec>> data,
      int fd,
      ::off_t offset,
      Receiver&& receiver)
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , write_submission(data, fd, offset) {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(static_cast<write_operation_base&&>(*this).receiver(), cqe.res);
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<write_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using write_operation = stoppable_task_facade<write_operation_base<Receiver>>;

  struct write_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    std::variant<::iovec, std::span<::iovec>> buffers_;
    int fd_;
    ::off_t offset_{-1};

    explicit write_sender(
      exec::io_uring_context& context,
      std::span<::iovec> data,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffers_{data}
      , fd_{fd}
      , offset_{offset} {
    }

    explicit write_sender(
      exec::io_uring_context& context,
      ::iovec data,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffers_{data}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    write_operation<Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return write_operation<Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }
  };

  struct byte_stream : native_fd_handle {
    using buffer_type = std::span<std::byte>;
    using buffers_type = std::span<buffer_type>;
    using const_buffer_type = std::span<const std::byte>;
    using const_buffers_type = std::span<const_buffer_type>;
    using extent_type = ::off_t;

    explicit byte_stream(native_fd_handle fd) noexcept
      : native_fd_handle{fd} {
    }

    write_sender write_some(async::write_some_t, const_buffers_type data) const noexcept {
      std::span<::iovec> buffers{reinterpret_cast<::iovec*>(data.data()), data.size()};
      return write_sender(*this->context_, buffers, this->fd_);
    }

    write_sender write_some(async::write_some_t, const_buffer_type data) const noexcept {
      ::iovec buffer = {
        .iov_base = const_cast<void*>(static_cast<const void*>(data.data())),
        .iov_len = data.size()};
      return write_sender(*this->context_, buffer, this->fd_);
    }

    auto write(async::write_t, const_buffers_type data) const noexcept {
      return reduce(buffered_sequence{write_some(async::write_some, data)}, 0ull);
    }

    auto write(async::write_t, const_buffer_type data) const noexcept {
      return reduce(buffered_sequence{write_some(async::write_some, data)}, 0ull);
    }

    read_sender read_some(async::read_some_t, buffers_type data) const noexcept {
      std::span<::iovec> buffers{reinterpret_cast<::iovec*>(data.data()), data.size()};
      return read_sender(*this->context_, buffers, this->fd_);
    }

    read_sender read_some(async::read_some_t, buffer_type data) const noexcept {
      ::iovec buffer = {.iov_base = data.data(), .iov_len = data.size()};
      return read_sender(*this->context_, buffer, this->fd_);
    }

    auto read(async::read_t, buffers_type data) const noexcept {
      return reduce(buffered_sequence{read_some(async::read_some, data)}, 0ull);
    }

    auto read(async::read_t, buffer_type data) const noexcept {
      return reduce(buffered_sequence{read_some(async::read_some, data)}, 0ull);
    }
  };

  struct seekable_byte_stream : byte_stream {
    using buffer_type = std::span<std::byte>;
    using buffers_type = std::span<buffer_type>;
    using const_buffer_type = std::span<const std::byte>;
    using const_buffers_type = std::span<const_buffer_type>;
    using extent_type = ::off_t;

    using byte_stream::byte_stream;

    using byte_stream::read_some;
    using byte_stream::read;
    using byte_stream::write_some;
    using byte_stream::write;

    write_sender
      write_some(async::write_some_t, const_buffers_type data, extent_type offset) const noexcept {
      std::span<::iovec> buffers{reinterpret_cast<::iovec*>(data.data()), data.size()};
      return write_sender{*this->context_, buffers, this->fd_, offset};
    }

    write_sender
      write_some(async::write_some_t, const_buffer_type data, extent_type offset) const noexcept {
      ::iovec buffer = {
        .iov_base = const_cast<void*>(static_cast<const void*>(data.data())),
        .iov_len = data.size()};
      return write_sender{*this->context_, buffer, this->fd_, offset};
    }

    read_sender
      read_some(async::read_some_t, buffers_type data, extent_type offset) const noexcept {
      std::span<::iovec> buffers{std::bit_cast<::iovec*>(data.data()), data.size()};
      return read_sender(*this->context_, buffers, this->fd_, offset);
    }

    read_sender read_some(async::read_some_t, buffer_type data, extent_type offset) const noexcept {
      ::iovec buffer = {.iov_base = data.data(), .iov_len = data.size()};
      return read_sender(*this->context_, buffer, this->fd_, offset);
    }

    auto write(async::write_t, const_buffers_type data, extent_type offset) const noexcept {
      return reduce(buffered_sequence{write_some(async::write_some, data, offset)}, 0ull);
    }

    auto write(async::write_t, const_buffer_type data, extent_type offset) const noexcept {
      return reduce(buffered_sequence{write_some(async::write_some, data, offset)}, 0ull);
    }

    auto read(async::read_t, buffers_type data, extent_type offset) const noexcept {
      return reduce(buffered_sequence{read_some(async::read_some, data, offset)}, 0ull);
    }

    auto read(async::read_t, buffer_type data, extent_type offset) const noexcept {
      return reduce(buffered_sequence{read_some(async::read_some, data, offset)}, 0ull);
    }
  };

  struct acceptor : native_fd_handle {
    ip::endpoint local_endpoint_;

    acceptor(exec::io_uring_context& context, int fd, const ip::endpoint& local_endpoint) noexcept
      : native_fd_handle(context, fd)
      , local_endpoint_(local_endpoint) {
    }
  };

  struct accept_submission {
    int fd_;
    ip::endpoint local_endpoint_;

    accept_submission(int fd, ip::endpoint local_endpoint) noexcept
      : fd_{fd}
      , local_endpoint_(static_cast<ip::endpoint&&>(local_endpoint)) {
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Receiver>
  struct accept_operation_base
    : stoppable_op_base<Receiver>
    , accept_submission {
    accept_operation_base(
      exec::io_uring_context& context,
      Receiver receiver,
      int fd,
      ip::endpoint local_endpoint) noexcept
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , accept_submission{fd, static_cast<ip::endpoint&&>(local_endpoint)} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<accept_operation_base&&>(*this).receiver(),
          byte_stream{
            native_fd_handle{this->context(), cqe.res}
        });
      } else {
        SIO_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<accept_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using accept_operation = stoppable_task_facade<accept_operation_base<Receiver>>;

  struct accept_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(byte_stream&&),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    int fd_;
    ip::endpoint local_endpoint_;

    template <stdexec::receiver_of<completion_signatures> Receiver>
    accept_operation<Receiver>
      connect(stdexec::connect_t, Receiver rcvr) noexcept(nothrow_decay_copyable<Receiver>) {
      return accept_operation<Receiver>{
        std::in_place,
        *context_,
        static_cast<Receiver&&>(rcvr),
        fd_,
        static_cast<ip::endpoint&&>(local_endpoint_)};
    }
  };

  struct path_handle : native_fd_handle {
    static path_handle current_directory() noexcept {
      return path_handle{
        native_fd_handle{nullptr, AT_FDCWD}
      };
    }
  };

  struct path_resource {
    exec::io_uring_context& context_;
    std::filesystem::path path_;

    explicit path_resource(exec::io_uring_context& context, std::filesystem::path path) noexcept
      : context_{context}
      , path_{static_cast<std::filesystem::path&&>(path)} {
    }

    auto open(async::open_t) const {
      open_data data_{path_, AT_FDCWD, O_PATH, 0};
      return stdexec::then(open_sender{context_, data_}, [](native_fd_handle fd) noexcept {
        return path_handle{fd};
      });
    }
  };

  struct file_resource {
    exec::io_uring_context& context_;
    open_data data_;

    explicit file_resource(
      exec::io_uring_context& context,
      std::filesystem::path path,
      path_handle base,
      async::mode mode,
      async::creation creation,
      async::caching caching) noexcept
      : context_{context}
      , data_{
          static_cast<std::filesystem::path&&>(path),
          base.fd_,
          static_cast<int>(creation),
          static_cast<mode_t>(mode)} {
    }

    auto open(async::open_t) const noexcept {
      return stdexec::then(open_sender{context_, data_}, [](native_fd_handle fd) noexcept {
        return seekable_byte_stream{fd};
      });
    }
  };

  struct io_scheduler {
    exec::io_uring_context* context_;

    using path_type = sio::io_uring::path_resource;
    using file_type = sio::io_uring::file_resource;

    path_type open_path(async::open_path_t, std::filesystem::path path) const noexcept {
      return path_type(*context_, static_cast<std::filesystem::path&&>(path));
    }

    file_type open_file(
      async::open_file_t,
      std::filesystem::path path,
      path_handle base,
      async::mode mode,
      async::creation creation,
      async::caching caching) const noexcept {
      return file_type{
        *context_, static_cast<std::filesystem::path&&>(path), base, mode, creation, caching};
    }
  };
}