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

#include "../sequence/buffered_sequence.hpp"
#include "../sequence/reduce.hpp"

#include "../const_buffer_span.hpp"
#include "../mutable_buffer_span.hpp"

#include "exec/linux/io_uring_context.hpp"
#include "sio/concepts.hpp"
#include "sio/ip/endpoint.hpp"

#include <exception>
#include <filesystem>
#include <span>

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

namespace sio::io_uring {
  struct env {
    exec::io_uring_scheduler scheduler;

    friend exec::io_uring_scheduler tag_invoke(
      stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
      const env& self) noexcept {
      return self.scheduler;
    }
  };

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

    env get_env(stdexec::get_env_t) const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct native_fd_handle {
    exec::io_uring_context* context_{};
    int fd_{-1};

    native_fd_handle() noexcept = default;

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

    env get_env(stdexec::get_env_t) const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct read_submission {
    mutable_buffer_span buffers_;
    int fd_;
    ::off_t offset_;

    read_submission(mutable_buffer_span buffers, int fd, ::off_t offset) noexcept;

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  struct read_submission_single {
    mutable_buffer buffers_;
    int fd_;
    ::off_t offset_;

    read_submission_single(mutable_buffer buffers, int fd, ::off_t offset) noexcept;

    ~read_submission_single();

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class SubmissionBase, class Receiver>
  struct io_operation_base
    : stoppable_op_base<Receiver>
    , SubmissionBase {
    io_operation_base(
      exec::io_uring_context& context,
      auto data,
      int fd,
      ::off_t offset,
      Receiver&& receiver) noexcept
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , SubmissionBase{data, fd, offset} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<io_operation_base&&>(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<Receiver&&>(this->__receiver_),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using read_operation = stoppable_task_facade<io_operation_base<read_submission, Receiver>>;

  template <class Receiver>
  using read_operation_single =
    stoppable_task_facade<io_operation_base<read_submission_single, Receiver>>;

  struct read_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    mutable_buffer_span buffers_;
    int fd_;
    ::off_t offset_;

    read_sender(
      exec::io_uring_context& context,
      mutable_buffer_span buffers,
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

    env get_env(stdexec::get_env_t) const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct read_sender_single {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    mutable_buffer buffers_;
    int fd_;
    ::off_t offset_;

    read_sender_single(
      exec::io_uring_context& context,
      mutable_buffer buffers,
      int fd,
      ::off_t offset = 0) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    read_operation_single<Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return read_operation_single<Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }

    env get_env(stdexec::get_env_t) const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct write_submission {
    const_buffer_span buffers_;
    int fd_;
    ::off_t offset_;

    write_submission(const_buffer_span buffers, int fd, ::off_t offset) noexcept;

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  struct write_submission_single {
    const_buffer buffers_;
    int fd_;
    ::off_t offset_;

    write_submission_single(const_buffer, int fd, ::off_t offset) noexcept;

    ~write_submission_single();

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Receiver>
  using write_operation = stoppable_task_facade<io_operation_base<write_submission, Receiver>>;

  template <class Receiver>
  using write_operation_single =
    stoppable_task_facade<io_operation_base<write_submission_single, Receiver>>;

  struct write_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    const_buffer_span buffers_;
    int fd_;
    ::off_t offset_{-1};

    explicit write_sender(
      exec::io_uring_context& context,
      const_buffer_span data,
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

    env get_env(stdexec::get_env_t) const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct write_sender_single {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    exec::io_uring_context* context_;
    const_buffer buffers_;
    int fd_;
    ::off_t offset_{-1};

    explicit write_sender_single(
      exec::io_uring_context& context,
      const_buffer data,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffers_{data}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    write_operation_single<Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return write_operation_single<Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }

    env get_env(stdexec::get_env_t) const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct write_factory {
    exec::io_uring_context* context_;
    int fd_;

    write_sender operator()(const_buffer_span data, ::off_t offset) const noexcept {
      return write_sender(*context_, data, fd_, offset);
    }

    write_sender_single operator()(const_buffer data, ::off_t offset) const noexcept {
      return write_sender_single(*context_, data, fd_, offset);
    }
  };

  struct read_factory {
    exec::io_uring_context* context_;
    int fd_;

    read_sender operator()(mutable_buffer_span data, ::off_t offset) const noexcept {
      return read_sender(*context_, data, fd_, offset);
    }

    read_sender_single operator()(mutable_buffer data, ::off_t offset) const noexcept {
      return read_sender_single(*context_, data, fd_, offset);
    }
  };

  struct byte_stream : native_fd_handle {
    using buffer_type = mutable_buffer;
    using buffers_type = mutable_buffer_span;
    using const_buffer_type = const_buffer;
    using const_buffers_type = const_buffer_span;
    using extent_type = ::off_t;

    using native_fd_handle::native_fd_handle;

    explicit byte_stream(const native_fd_handle& fd) noexcept
      : native_fd_handle{fd} {
    }

    write_sender write_some(async::write_some_t, const_buffers_type data) const noexcept {
      return write_sender(*this->context_, data, this->fd_);
    }

    write_sender_single write_some(async::write_some_t, const_buffer_type data) const noexcept {
      return write_sender_single(*this->context_, data, this->fd_);
    }

    auto write(async::write_t, const_buffers_type data) const noexcept {
      return reduce(
        buffered_sequence<write_factory, const_buffer_span>(
          write_factory{this->context_, this->fd_}, data),
        0ull);
    }

    auto write(async::write_t, const_buffer_type data) const noexcept {
      return reduce(
        buffered_sequence<write_factory, const_buffer>(
          write_factory{this->context_, this->fd_}, data),
        0ull);
    }

    read_sender read_some(async::read_some_t, buffers_type buffer) const noexcept {
      return read_sender(*this->context_, buffer, this->fd_);
    }

    read_sender_single read_some(async::read_some_t, buffer_type buffer) const noexcept {
      return read_sender_single(*this->context_, buffer, this->fd_);
    }

    auto read(async::read_t, buffers_type buffer) const noexcept {
      return reduce(
        buffered_sequence<read_factory, mutable_buffer_span>(
          read_factory{this->context_, this->fd_}, buffer),
        0ull);
    }

    auto read(async::read_t, buffer_type buffer) const noexcept {
      return reduce(
        buffered_sequence<read_factory, mutable_buffer>(
          read_factory{this->context_, this->fd_}, buffer),
        0ull);
    }
  };

  struct seekable_byte_stream : byte_stream {
    using buffer_type = mutable_buffer;
    using buffers_type = mutable_buffer_span;
    using const_buffer_type = const_buffer;
    using const_buffers_type = const_buffer_span;
    using offset_type = ::off_t;
    using extent_type = ::off_t;

    using byte_stream::byte_stream;
    using byte_stream::read_some;
    using byte_stream::read;
    using byte_stream::write_some;
    using byte_stream::write;

    write_sender write_some(async::write_some_t, const_buffers_type buffers, extent_type offset)
      const noexcept {
      return write_sender{*this->context_, buffers, this->fd_, offset};
    }

    write_sender_single
      write_some(async::write_some_t, const_buffer_type buffer, extent_type offset) const noexcept {
      return write_sender_single{*this->context_, buffer, this->fd_, offset};
    }

    read_sender
      read_some(async::read_some_t, buffers_type buffers, extent_type offset) const noexcept {
      return read_sender(*this->context_, buffers, this->fd_, offset);
    }

    read_sender_single
      read_some(async::read_some_t, buffer_type buffer, extent_type offset) const noexcept {
      return read_sender_single(*this->context_, buffer, this->fd_, offset);
    }

    auto write(async::write_t, const_buffers_type data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<write_factory, const_buffer_span>(
          write_factory{this->context_, this->fd_}, data, offset),
        0ull);
    }

    auto write(async::write_t, const_buffer_type data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<write_factory, const_buffer>(
          write_factory{this->context_, this->fd_}, data, offset),
        0ull);
    }

    auto read(async::read_t, buffers_type data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<read_factory, mutable_buffer_span>(
          read_factory{this->context_, this->fd_}, data, offset),
        0ull);
    }

    auto read(async::read_t, buffer_type data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<read_factory, mutable_buffer>(
          read_factory{this->context_, this->fd_}, data, offset),
        0ull);
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