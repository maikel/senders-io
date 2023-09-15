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
#include "../ip/endpoint.hpp"
#include "../sequence/reduce.hpp"

#include "./buffered_sequence.hpp"
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

  template <class Context, class Receiver>
  struct read_operation_base
    : stoppable_op_base<Context, Receiver>
    , read_submission {
    read_operation_base(
      Context& context,
      std::variant<::iovec, std::span<::iovec>> data,
      int fd,
      ::off_t offset,
      Receiver&& receiver) noexcept
      : stoppable_op_base<Context, Receiver>{context, static_cast<Receiver&&>(receiver)}
      , read_submission{data, fd, offset} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<read_operation_base&&>(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<read_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Context, class Receiver>
  using read_operation = stoppable_task_facade_t<read_operation_base<Context, Receiver>>;

  template <class Context>
  struct read_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    std::variant<::iovec, std::span<::iovec>> buffers_;
    int fd_;
    ::off_t offset_;

    read_sender(Context& context, std::span<::iovec> buffers, int fd, ::off_t offset = 0) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    read_sender(Context& context, ::iovec buffers, int fd, ::off_t offset = 0) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    read_operation<Context, Receiver> connect(stdexec::connect_t, Receiver rcvr) const noexcept {
      return read_operation<Context, Receiver>{
        std::in_place, *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(rcvr)};
    }

    env<Context> get_env(stdexec::get_env_t) const noexcept {
      return {context_};
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

  template <class Context, class Receiver>
  struct write_operation_base
    : stoppable_op_base<Context, Receiver>
    , write_submission {
    write_operation_base(
      Context& context,
      std::variant<::iovec, std::span<::iovec>> data,
      int fd,
      ::off_t offset,
      Receiver&& receiver)
      : stoppable_op_base<Context, Receiver>{context, static_cast<Receiver&&>(receiver)}
      , write_submission(data, fd, offset) {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        stdexec::set_value(
          static_cast<write_operation_base&&>(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<write_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Context, class Receiver>
  using write_operation = stoppable_task_facade_t<write_operation_base<Context, Receiver>>;

  template <class Context>
  struct write_sender {
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    Context* context_;
    std::variant<::iovec, std::span<::iovec>> buffers_;
    int fd_;
    ::off_t offset_{-1};

    explicit write_sender(
      Context& context,
      std::span<::iovec> data,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffers_{data}
      , fd_{fd}
      , offset_{offset} {
    }

    explicit write_sender(Context& context, ::iovec data, int fd, ::off_t offset = -1) noexcept
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
  struct basic_byte_stream : basic_native_fd_handle<Context> {
    using buffer_type = std::span<std::byte>;
    using buffers_type = std::span<buffer_type>;
    using const_buffer_type = std::span<const std::byte>;
    using const_buffers_type = std::span<const_buffer_type>;
    using extent_type = ::off_t;

    using basic_native_fd_handle<Context>::basic_native_fd_handle;

    explicit basic_byte_stream(const basic_native_fd_handle<Context>& fd) noexcept
      : basic_native_fd_handle<Context>(fd) {
    }

    write_sender<Context> write_some(async::write_some_t, const_buffers_type data) const noexcept {
      std::span<::iovec> buffers{reinterpret_cast<::iovec*>(data.data()), data.size()};
      return write_sender(*this->context_, buffers, this->fd_);
    }

    write_sender<Context> write_some(async::write_some_t, const_buffer_type data) const noexcept {
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

    read_sender<Context> read_some(async::read_some_t, buffers_type data) const noexcept {
      std::span<::iovec> buffers{reinterpret_cast<::iovec*>(data.data()), data.size()};
      return read_sender(*this->context_, buffers, this->fd_);
    }

    read_sender<Context> read_some(async::read_some_t, buffer_type data) const noexcept {
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

  using byte_stream = basic_byte_stream<exec::io_uring_context>;

  template <class Context>
  struct basic_seekable_byte_stream : basic_byte_stream<Context> {
    using buffer_type = std::span<std::byte>;
    using buffers_type = std::span<buffer_type>;
    using const_buffer_type = std::span<const std::byte>;
    using const_buffers_type = std::span<const_buffer_type>;
    using offset_type = ::off_t;
    using extent_type = ::off_t;

    using basic_byte_stream<Context>::basic_byte_stream;
    using basic_byte_stream<Context>::read_some;
    using basic_byte_stream<Context>::read;
    using basic_byte_stream<Context>::write_some;
    using basic_byte_stream<Context>::write;

    write_sender<Context>
      write_some(async::write_some_t, const_buffers_type data, extent_type offset) const noexcept {
      std::span<::iovec> buffers{reinterpret_cast<::iovec*>(data.data()), data.size()};
      return write_sender{*this->context_, buffers, this->fd_, offset};
    }

    write_sender<Context>
      write_some(async::write_some_t, const_buffer_type data, extent_type offset) const noexcept {
      ::iovec buffer = {
        .iov_base = const_cast<void*>(static_cast<const void*>(data.data())),
        .iov_len = data.size()};
      return write_sender{*this->context_, buffer, this->fd_, offset};
    }

    read_sender<Context>
      read_some(async::read_some_t, buffers_type data, extent_type offset) const noexcept {
      std::span<::iovec> buffers{std::bit_cast<::iovec*>(data.data()), data.size()};
      return read_sender(*this->context_, buffers, this->fd_, offset);
    }

    read_sender<Context>
      read_some(async::read_some_t, buffer_type data, extent_type offset) const noexcept {
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