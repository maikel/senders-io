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
#include "../concepts.hpp"
#include "../io_concepts.hpp"

#include "../sequence/buffered_sequence.hpp"
#include "../sequence/reduce.hpp"

#include "../const_buffer_span.hpp"
#include "../mutable_buffer_span.hpp"

#include "./io_uring_context.hpp"

#include <exception>
#include <filesystem>
#include <span>

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

namespace sio::io_uring {

  template <class Context>
  struct env {
    Context* context_;

    friend auto tag_invoke(
      stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
      const env& self) noexcept {
      return self.context_->get_scheduler();
    }
  };

  struct close_submission {
    int fd_;

    close_submission(int fd) noexcept
      : fd_{fd} {
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Context, class Receiver>
  struct close_operation_base
    : stoppable_op_base<Context, Receiver>
    , close_submission {
    close_operation_base(Context& context, Receiver receiver, int fd)
      : stoppable_op_base<Context, Receiver>{context, static_cast<Receiver&&>(receiver)}
      , close_submission{fd} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res == 0) {
        stdexec::set_value(static_cast<close_operation_base&&>(*this).receiver());
      } else {
        SIO_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<close_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Context, class Receiver>
  using close_operation = io_task_facade<close_operation_base<Context, Receiver>>;

  template <class Context>
  struct close_sender {
    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    int fd_;

    template <class Receiver>
    close_operation<Context, Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return close_operation<Context, Receiver>{
        std::in_place, *context_, static_cast<Receiver&&>(rcvr), fd_};
    }

    env<Context> get_env(stdexec::get_env_t) const noexcept {
      return {context_};
    }
  };

  template <class Context>
  struct basic_native_fd_handle {
    Context* context_{};
    int fd_{-1};

    basic_native_fd_handle() noexcept = default;

    explicit basic_native_fd_handle(Context* context, int fd) noexcept
      : context_{context}
      , fd_{fd} {
    }

    explicit basic_native_fd_handle(Context& context, int fd) noexcept
      : context_{&context}
      , fd_{fd} {
    }

    int get() const noexcept {
      return fd_;
    }

    close_sender<Context> close(async::close_t) const noexcept {
      return {context_, fd_};
    }
  };

  using native_fd_handle = basic_native_fd_handle<exec::io_uring_context>;

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

  template <class Context, class Receiver>
  struct open_operation_base
    : stoppable_op_base<Context, Receiver>
    , open_submission {

    open_operation_base(open_data data, Context& context, Receiver&& receiver)
      : stoppable_op_base<Context, Receiver>{context, static_cast<Receiver&&>(receiver)}
      , open_submission{static_cast<open_data&&>(data)} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<open_operation_base&&>(*this).receiver(),
          basic_native_fd_handle{&this->context(), cqe.res});
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<open_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Context, class Receiver>
  using open_operation = stoppable_task_facade_t<open_operation_base<Context, Receiver>>;

  template <class Context>
  struct open_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(basic_native_fd_handle<Context>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    open_data data_;

    explicit open_sender(Context& context, open_data data) noexcept
      : context_{&context}
      , data_{static_cast<open_data&&>(data)} {
    }

    template <decays_to<open_sender> Self, stdexec::receiver_of<completion_signatures> Receiver>
    static open_operation<Context, Receiver>
      connect(Self&& self, stdexec::connect_t, Receiver rcvr) noexcept {
      return {
        std::in_place,
        static_cast<Self&&>(self).data_,
        *self.context_,
        static_cast<Receiver&&>(rcvr)};
    }

    env<Context> get_env(stdexec::get_env_t) const noexcept {
      return env<Context>{context_};
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

  template <class SubmissionBase, class Context, class Receiver>
  struct io_operation_base
    : stoppable_op_base<Context, Receiver>
    , SubmissionBase {
    io_operation_base(
      Context& context,
      auto data,
      int fd,
      ::off_t offset,
      Receiver&& receiver) noexcept
      : stoppable_op_base<Context, Receiver>{context, static_cast<Receiver&&>(receiver)}
      , SubmissionBase{data, fd, offset} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<io_operation_base&&>(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<io_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Context, class Receiver>
  using read_operation =
    stoppable_task_facade_t<io_operation_base<read_submission, Context, Receiver>>;

  template <class Context, class Receiver>
  using read_operation_single =
    stoppable_task_facade_t<io_operation_base<read_submission_single, Context, Receiver>>;

  template <class Context>
  struct read_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    mutable_buffer_span buffers_;
    int fd_;
    ::off_t offset_;

    read_sender(Context& context, mutable_buffer_span buffers, int fd, ::off_t offset = 0) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    read_operation<Context, Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return read_operation<Context, Receiver>(
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr));
    }

    env<Context> get_env(stdexec::get_env_t) const noexcept {
      return {context_};
    }
  };

  template <class Context>
  struct read_sender_single {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    mutable_buffer buffers_;
    int fd_;
    ::off_t offset_;

    read_sender_single(
      Context& context,
      mutable_buffer buffers,
      int fd,
      ::off_t offset = 0) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    read_operation_single<Context, Receiver>
      connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return read_operation_single<Context, Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }

    env<Context> get_env(stdexec::get_env_t) const noexcept {
      return {context_};
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

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept;
  };

  template <class Context, class Receiver>
  using write_operation =
    stoppable_task_facade_t<io_operation_base<write_submission, Context, Receiver>>;

  template <class Context, class Receiver>
  using write_operation_single =
    stoppable_task_facade_t<io_operation_base<write_submission_single, Context, Receiver>>;

  template <class Context>
  struct write_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    const_buffer_span buffers_;
    int fd_;
    ::off_t offset_{-1};

    explicit write_sender(
      Context& context,
      const_buffer_span data,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffers_{data}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    write_operation<Context, Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return write_operation<Context, Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }

    env<Context> get_env(stdexec::get_env_t) const noexcept {
      return {context_};
    }
  };

  template <class Context>
  struct write_sender_single {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    const_buffer buffers_;
    int fd_;
    ::off_t offset_{-1};

    explicit write_sender_single(
      Context& context,
      const_buffer data,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffers_{data}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    write_operation_single<Context, Receiver>
      connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return write_operation_single<Context, Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }

    env<Context> get_env(stdexec::get_env_t) const noexcept {
      return {context_};
    }
  };

  template <class Context>
  struct write_factory {
    Context* context_;
    int fd_;

    write_sender<Context> operator()(const_buffer_span data, ::off_t offset) const noexcept {
      return write_sender<Context>(*context_, data, fd_, offset);
    }

    write_sender_single<Context> operator()(const_buffer data, ::off_t offset) const noexcept {
      return write_sender_single<Context>(*context_, data, fd_, offset);
    }
  };

  template <class Context>
  struct read_factory {
    Context* context_;
    int fd_;

    read_sender<Context> operator()(mutable_buffer_span data, ::off_t offset) const noexcept {
      return read_sender<Context>(*context_, data, fd_, offset);
    }

    read_sender_single<Context> operator()(mutable_buffer data, ::off_t offset) const noexcept {
      return read_sender_single<Context>(*context_, data, fd_, offset);
    }
  };

  template <class Context>
  struct basic_byte_stream : basic_native_fd_handle<Context> {
    using buffer_type = mutable_buffer;
    using buffers_type = mutable_buffer_span;
    using const_buffer_type = const_buffer;
    using const_buffers_type = const_buffer_span;
    using extent_type = ::off_t;

    using basic_native_fd_handle<Context>::basic_native_fd_handle;

    explicit basic_byte_stream(const basic_native_fd_handle<Context>& fd) noexcept
      : basic_native_fd_handle<Context>(fd) {
    }

    write_sender<Context> write_some(async::write_some_t, const_buffers_type data) const noexcept {
      return write_sender<Context>(*this->context_, data, this->fd_);
    }

    write_sender_single<Context>
      write_some(async::write_some_t, const_buffer_type data) const noexcept {
      return write_sender_single<Context>(*this->context_, data, this->fd_);
    }

    auto write(async::write_t, std::span<const_buffer> data) const noexcept {
      return reduce(
        buffered_sequence<write_factory<Context>, std::span<const_buffer>>(
          write_factory<Context>{this->context_, this->fd_}, data),
        0ull);
    }

    auto write(async::write_t, const_buffer_type data) const noexcept {
      return reduce(
        buffered_sequence<write_factory<Context>, const_buffer>(
          write_factory<Context>{this->context_, this->fd_}, data),
        0ull);
    }

    read_sender<Context> read_some(async::read_some_t, buffers_type buffer) const noexcept {
      return read_sender(*this->context_, buffer, this->fd_);
    }

    read_sender_single<Context> read_some(async::read_some_t, buffer_type buffer) const noexcept {
      return read_sender_single(*this->context_, buffer, this->fd_);
    }

    auto read(async::read_t, std::span<mutable_buffer> buffer) const noexcept {
      return reduce(
        buffered_sequence<read_factory<Context>, std::span<mutable_buffer>>(
          read_factory<Context>{this->context_, this->fd_}, buffer),
        0ull);
    }

    auto read(async::read_t, buffer_type buffer) const noexcept {
      return reduce(
        buffered_sequence<read_factory<Context>, mutable_buffer>(
          read_factory<Context>{this->context_, this->fd_}, buffer),
        0ull);
    }
  };

  using byte_stream = basic_byte_stream<exec::io_uring_context>;

  template <class Context>
  struct basic_seekable_byte_stream : basic_byte_stream<Context> {
    using buffer_type = mutable_buffer;
    using buffers_type = mutable_buffer_span;
    using const_buffer_type = const_buffer;
    using const_buffers_type = const_buffer_span;
    using offset_type = ::off_t;
    using extent_type = ::off_t;

    using basic_byte_stream<Context>::basic_byte_stream;
    using basic_byte_stream<Context>::read_some;
    using basic_byte_stream<Context>::read;
    using basic_byte_stream<Context>::write_some;
    using basic_byte_stream<Context>::write;

    write_sender<Context> write_some(
      async::write_some_t,
      const_buffers_type buffers,
      extent_type offset) const noexcept {
      return write_sender<Context>{*this->context_, buffers, this->fd_, offset};
    }

    write_sender_single<Context>
      write_some(async::write_some_t, const_buffer_type buffer, extent_type offset) const noexcept {
      return write_sender_single<Context>{*this->context_, buffer, this->fd_, offset};
    }

    read_sender<Context>
      read_some(async::read_some_t, buffers_type buffers, extent_type offset) const noexcept {
      return read_sender<Context>{*this->context_, buffers, this->fd_, offset};
    }

    read_sender_single<Context>
      read_some(async::read_some_t, buffer_type buffer, extent_type offset) const noexcept {
      return read_sender_single<Context>{*this->context_, buffer, this->fd_, offset};
    }

    auto write(async::write_t, std::span<const_buffer> data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<write_factory<Context>, std::span<const_buffer>>(
          write_factory<Context>{this->context_, this->fd_}, data, offset),
        0ull);
    }

    auto write(async::write_t, const_buffer_type data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<write_factory<Context>, const_buffer>(
          write_factory<Context>{this->context_, this->fd_}, data, offset),
        0ull);
    }

    auto read(async::read_t, std::span<mutable_buffer> data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<read_factory<Context>, std::span<mutable_buffer>>(
          read_factory<Context>{this->context_, this->fd_}, data, offset),
        0ull);
    }

    auto read(async::read_t, buffer_type data, extent_type offset) const noexcept {
      return reduce(
        buffered_sequence<read_factory<Context>, mutable_buffer>(
          read_factory<Context>{this->context_, this->fd_}, data, offset),
        0ull);
    }
  };

  using seekable_byte_stream = basic_seekable_byte_stream<exec::io_uring_context>;

  template <class Context>
  struct basic_path_handle : basic_native_fd_handle<Context> {
    static basic_path_handle<Context> current_directory() noexcept {
      return basic_path_handle{
        basic_native_fd_handle<Context>{nullptr, AT_FDCWD}
      };
    }
  };

  using path_handle = basic_path_handle<exec::io_uring_context>;

  template <class Context>
  struct basic_path_resource {
    Context& context_;
    std::filesystem::path path_;

    explicit basic_path_resource(Context& context, std::filesystem::path path) noexcept
      : context_{context}
      , path_{static_cast<std::filesystem::path&&>(path)} {
    }

    auto open(async::open_t) const {
      open_data data_{path_, AT_FDCWD, O_PATH, 0};
      return stdexec::then(
        open_sender{context_, data_},
        [](basic_native_fd_handle<Context> fd) noexcept { return basic_path_handle<Context>{fd}; });
    }
  };

  using path_resource = basic_path_resource<exec::io_uring_context>;

  template <class Context>
  struct basic_file_resource {
    Context& context_;
    open_data data_;

    explicit basic_file_resource(
      Context& context,
      std::filesystem::path path,
      basic_path_handle<Context> base,
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
      return stdexec::then(
        open_sender{context_, data_}, [](basic_native_fd_handle<Context> fd) noexcept {
          return basic_seekable_byte_stream<Context>{fd};
        });
    }
  };

  using file_resource = basic_file_resource<exec::io_uring_context>;

  template <class Context>
  struct basic_io_scheduler {
    Context* context_;

    using path_type = sio::io_uring::basic_path_resource<Context>;
    using file_type = sio::io_uring::basic_file_resource<Context>;

    path_type open_path(async::open_path_t, std::filesystem::path path) const noexcept {
      return path_type(*context_, static_cast<std::filesystem::path&&>(path));
    }

    file_type open_file(
      async::open_file_t,
      std::filesystem::path path,
      basic_path_handle<Context> base,
      async::mode mode,
      async::creation creation,
      async::caching caching) const noexcept {
      return file_type{
        *context_, static_cast<std::filesystem::path&&>(path), base, mode, creation, caching};
    }
  };

  using io_scheduler = basic_io_scheduler<exec::io_uring_context>;
}