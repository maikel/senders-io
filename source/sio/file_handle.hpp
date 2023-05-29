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

#include "./io_concepts.hpp"

#include "exec/linux/io_uring_context.hpp"

#include <exception>
#include <filesystem>
#include <span>

#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

namespace exec { namespace __io_uring {
  using namespace stdexec;

  template <class Receiver>
  struct close_operation_base : __stoppable_op_base<Receiver> {
    int fd_;

    close_operation_base(int fd, io_uring_context& context, Receiver receiver)
      : __stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , fd_{fd} {
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CLOSE;
      sqe_.fd = fd_;
      sqe = sqe_;
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res == 0) {
        stdexec::set_value(static_cast<Receiver&&>(this->__receiver_), cqe.res);
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        stdexec::set_error(
          static_cast<Receiver&&>(this->__receiver_),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using close_operation = __stoppable_task_facade_t<close_operation_base<Receiver>>;

  struct close_sender {
    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_uring_context* context_;
    int fd_;

    template <class Receiver>
    static close_operation<Receiver> connect(close_sender self, connect_t, Receiver rcvr) noexcept {
      return close_operation<Receiver>{
        std::in_place, self.fd_, *self.context_, static_cast<Receiver&&>(rcvr)};
    }
  };

  struct native_fd_handle {
    io_uring_context* context_;
    int fd_;

    native_fd_handle(io_uring_context& context, int fd) noexcept
      : context_{&context}
      , fd_{fd} {
    }

    close_sender close(exec::async_resource::close_t) const noexcept {
      return {handle.context_, handle.fd_};
    }
  };

  struct open_data {
    std::filesystem::path path_;
    int dirfd_{0};
    int flags_{0};
    ::mode_t mode_{0};
  };

  template <class _RcvrId>
  struct __open_operation {
    using Receiver = stdexec::__t<_RcvrId>;

    struct __impl : __stoppable_op_base<Receiver> {
      open_data data_;

      __impl(open_data data, io_uring_context& context, Receiver&& receiver)
        : __stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
        , data_{static_cast<open_data&&>(data)} {
      }

      static constexpr std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& sqe) noexcept {
        ::io_uring_sqe sqe_{};
        sqe_.opcode = IORING_OP_OPENAT;
        sqe_.addr = bit_cast<__u64>(data_.path_.c_str());
        sqe_.open_flags = O_PATH;
        sqe = sqe_;
      }

      void complete(const ::io_uring_cqe& cqe) noexcept {
        if (cqe.res >= 0) {
          stdexec::set_value(
            static_cast<Receiver&&>(this->__receiver_), native_fd_handle{&context_, cqe.res});
        } else {
          STDEXEC_ASSERT(cqe.res < 0);
          stdexec::set_error(
            static_cast<Receiver&&>(this->__receiver_),
            std::make_exception_ptr(std::system_error(-cqe.res, std::system_category())));
        }
      }
    };

    using __t = __stoppable_task_facade_t<__impl>;
  };

  struct __open_sender {
    using __id = __open_sender;
    using __t = __open_sender;
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(native_fd_handle),
      stdexec::set_error_t(std::exception_ptr),
      stdexec::set_stopped_t()>;

    template <class Receiver>
    using __operation_t =
      stdexec::__t<__open_operation<stdexec::__id<stdexec::__decay_t<Receiver>>>>;

    io_uring_context* context_;
    open_data data_;

    explicit __open_sender(io_uring_context& context, open_data data) noexcept
      : context_{&context}
      , data_{static_cast<open_data&&>(data)} {
    }

    template <__decays_to<__open_sender> _Self, class _Rcvr>
    friend auto tag_invoke(connect_t, _Self&& __sender, _Rcvr&& __rcvr) noexcept {
      return __operation_t<_Rcvr>{
        std::in_place,
        static_cast<_Self&&>(__sender).data_,
        *__sender.context_,
        static_cast<_Rcvr&&>(__rcvr)};
    }
  };

  template <class _RcvrId>
  struct __read_operation {
    using Receiver = stdexec::__t<_RcvrId>;

    struct __impl : __stoppable_op_base<Receiver> {
      std::span<std::span<std::byte>> __buffers_;
      int fd_;

      __impl(std::span<::iovec> data, io_uring_context& context, Receiver&& receiver)
        : __stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
        , __buffers_{data} {
      }

      static constexpr std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& sqe) noexcept {
        ::io_uring_sqe sqe_{};
        sqe_.opcode = IORING_OP_READV;
        sqe_.fd = fd_;
        sqe_.addr = bit_cast<__u64>(__buffers_.data());
        sqe_.len = __buffers_.size();
      }

      void complete(const ::io_uring_cqe& cqe) noexcept {
        if (cqe.res >= 0) {
          stdexec::set_value(static_cast<Receiver&&>(this->__receiver_), );
        } else {
          STDEXEC_ASSERT(cqe.res < 0);
          stdexec::set_error(
            static_cast<Receiver&&>(this->__receiver_),
            std::make_exception_ptr(std::system_error(-cqe.res, std::system_category())));
        }
      }
    };

    using __t = __stoppable_task_facade_t<__impl>;
  };

  struct __read_sender {
    using __id = __read_sender;
    using __t = __read_sender;
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(native_fd_handle),
      stdexec::set_error_t(std::exception_ptr),
      stdexec::set_stopped_t()>;

    template <class Receiver>
    using __operation_t =
      stdexec::__t<__open_operation<stdexec::__id<stdexec::__decay_t<Receiver>>>>;

    io_uring_context* context_;
    open_data data_;

    explicit __read_sender(io_uring_context& context, open_data data) noexcept
      : context_{&context}
      , data_{static_cast<open_data&&>(data)} {
    }

    template <__decays_to<__read_sender> _Self, class _Rcvr>
    friend auto tag_invoke(connect_t, _Self&& __sender, _Rcvr&& __rcvr) noexcept {
      return __operation_t<_Rcvr>{
        std::in_place,
        static_cast<_Self&&>(__sender).data_,
        *__sender.context_,
        static_cast<_Rcvr&&>(__rcvr)};
    }
  };

  template <class _RcvrId>
  struct __write_operation {
    using Receiver = stdexec::__t<_RcvrId>;

    struct __impl : __stoppable_op_base<Receiver> {
      std::span<std::span<std::byte>> __buffers_;
      int fd_;

      __impl(std::span<::iovec> data, io_uring_context& context, Receiver&& receiver)
        : __stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
        , __buffers_{data} {
      }

      static constexpr std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& sqe) noexcept {
        ::io_uring_sqe sqe_{};
        sqe_.opcode = IORING_OP_WRITEV;
        sqe_.fd = fd_;
        sqe_.addr = bit_cast<__u64>(__buffers_.data());
        sqe_.len = __buffers_.size();
      }

      void complete(const ::io_uring_cqe& cqe) noexcept {
        if (cqe.res >= 0) {
          stdexec::set_value(static_cast<Receiver&&>(this->__receiver_), );
        } else {
          STDEXEC_ASSERT(cqe.res < 0);
          stdexec::set_error(
            static_cast<Receiver&&>(this->__receiver_),
            std::make_exception_ptr(std::system_error(-cqe.res, std::system_category())));
        }
      }
    };

    using __t = __stoppable_task_facade_t<__impl>;
  };

  struct __write_sender {
    using __id = __write_sender;
    using __t = __write_sender;
    using is_sender = void;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(native_fd_handle),
      stdexec::set_error_t(std::exception_ptr),
      stdexec::set_stopped_t()>;

    template <class Receiver>
    using __operation_t =
      stdexec::__t<__open_operation<stdexec::__id<stdexec::__decay_t<Receiver>>>>;

    io_uring_context* context_;
    open_data data_;

    explicit __write_sender(io_uring_context& context, open_data data) noexcept
      : context_{&context}
      , data_{static_cast<open_data&&>(data)} {
    }

    template <__decays_to<__write_sender> _Self, class _Rcvr>
    friend auto tag_invoke(connect_t, _Self&& __sender, _Rcvr&& __rcvr) noexcept {
      return __operation_t<_Rcvr>{
        std::in_place,
        static_cast<_Self&&>(__sender).data_,
        *__sender.context_,
        static_cast<_Rcvr&&>(__rcvr)};
    }
  };

  struct __file_handle {
    native_fd_handle native_;

    using buffer_type = std::span<std::byte>;
    using buffers_type = std::span<buffer_type>;
    using const_buffer_type = std::span<const std::byte>;
    using const_buffers_type = std::span<const_buffer_type>;
    using extent_type = ::off_t;

    close_sender close(async_resource::close_t) const noexcept {
      return native_.close(async_resource::close);
    }

    friend __write_sender tag_invoke(
      async::write_t,
      __file_handle handle,
      const_buffers_type data,
      extent_type __offset) noexcept;

    friend __read_sender tag_invoke(
      async::read_t,
      __file_handle handle,
      buffers_type data,
      extent_type __offset) noexcept;
  };

  struct __file {
    io_uring_context& context_;
    open_data data_;

    explicit __file(
      io_uring_context& context,
      std::filesystem::path __path,
      int __dirfd,
      int __flags,
      ::mode_t __mode) noexcept
      : context_{context}
      , data_{static_cast<std::filesystem::path&&>(__path), __dirfd, __flags, __mode} {
    }

    friend __open_sender tag_invoke(exec::async_resource::open_t, const __file& __file) noexcept {
      return __open_sender{__file.context_, __file.data_};
    }
  };

  struct __io_scheduler {
    io_uring_context* context_;

    using path_type = __file;
    using file_type = __file;

    template <same_as<async::file_t> _File, same_as<__io_scheduler> _Scheduler>
    friend file_type tag_invoke(_File, _Scheduler __self) noexcept {
      return file_type{
        *__self.context_,
        open_data{static_cast<std::filesystem::path&&>(__path), __dirfd, __flags, __mode}
      };
    }
  };
}}
