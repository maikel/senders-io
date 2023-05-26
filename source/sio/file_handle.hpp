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

  template <class _ReceiverId>
  struct __close_operation {
    using _Receiver = stdexec::__t<_ReceiverId>;

    struct __impl : __stoppable_op_base<_Receiver> {
      int __fd_;

      __impl(int __fd, io_uring_context& __context, _Receiver&& __receiver)
        : __stoppable_op_base<_Receiver>{__context, static_cast<_Receiver&&>(__receiver)}
        , __fd_{__fd} {
      }

      static constexpr std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& __sqe) noexcept {
        ::io_uring_sqe __sqe_{};
        __sqe_.opcode = IORING_OP_CLOSE;
        __sqe_.fd = __fd_;
        __sqe = __sqe_;
      }

      void complete(const ::io_uring_cqe& __cqe) noexcept {
        if (__cqe.res == 0) {
          stdexec::set_value(static_cast<_Receiver&&>(this->__receiver_), __cqe.res);
        } else {
          STDEXEC_ASSERT(__cqe.res < 0);
          stdexec::set_error(
            static_cast<_Receiver&&>(this->__receiver_),
            std::make_exception_ptr(std::system_error(-__cqe.res, std::system_category())));
        }
      }
    };

    using __t = __stoppable_task_facade_t<__impl>;
  };

  struct __close_sender {
    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::exception_ptr),
      stdexec::set_stopped_t()>;

    template <class _Receiver>
    using __operation_t = __t<__close_operation<__id<__decay_t<_Receiver>>>>;

    io_uring_context* __context_;
    int __fd_;

    template <__decays_to<__close_sender> _Self, class _Rcvr>
    friend auto tag_invoke(connect_t, _Self&& __self, _Rcvr&& __rcvr) noexcept {
      return __operation_t<_Rcvr>{
        std::in_place, __self.__fd_, *__self.__context_, static_cast<_Rcvr&&>(__rcvr)};
    }
  };

  struct native_fd_handle {
    io_uring_context* __context_;
    int __fd_;

    native_fd_handle(io_uring_context& __context, int __fd) noexcept
      : __context_{&__context}
      , __fd_{__fd} {
    }

    friend __close_sender
      tag_invoke(exec::async_resource::close_t, native_fd_handle __handle) noexcept {
      return {__handle.__context_, __handle.__fd_};
    }
  };

  struct __open_data {
    std::filesystem::path __path_;
    int __dirfd_{0};
    int __flags_{0};
    ::mode_t __mode_{0};
  };

  template <class _RcvrId>
  struct __open_operation {
    using _Receiver = stdexec::__t<_RcvrId>;

    struct __impl : __stoppable_op_base<_Receiver> {
      __open_data __data_;

      __impl(__open_data __data, io_uring_context& __context, _Receiver&& __receiver)
        : __stoppable_op_base<_Receiver>{__context, static_cast<_Receiver&&>(__receiver)}
        , __data_{static_cast<__open_data&&>(__data)} {
      }

      static constexpr std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& __sqe) noexcept {
        ::io_uring_sqe __sqe_{};
        __sqe_.opcode = IORING_OP_OPENAT;
        __sqe_.addr = bit_cast<__u64>(__data.__path_.c_str());
        __sqe_.open_flags = O_PATH;
        __sqe = __sqe_;
      }

      void complete(const ::io_uring_cqe& __cqe) noexcept {
        if (__cqe.res >= 0) {
          stdexec::set_value(
            static_cast<_Receiver&&>(this->__receiver_), native_fd_handle{&__context_, __cqe.res});
        } else {
          STDEXEC_ASSERT(__cqe.res < 0);
          stdexec::set_error(
            static_cast<_Receiver&&>(this->__receiver_),
            std::make_exception_ptr(std::system_error(-__cqe.res, std::system_category())));
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

    template <class _Receiver>
    using __operation_t =
      stdexec::__t<__open_operation<stdexec::__id<stdexec::__decay_t<_Receiver>>>>;

    io_uring_context* __context_;
    __open_data __data_;

    explicit __open_sender(io_uring_context& __context, __open_data __data) noexcept
      : __context_{&__context}
      , __data_{static_cast<__open_data&&>(__data)} {
    }

    template <__decays_to<__open_sender> _Self, class _Rcvr>
    friend auto tag_invoke(connect_t, _Self&& __sender, _Rcvr&& __rcvr) noexcept {
      return __operation_t<_Rcvr>{
        std::in_place,
        static_cast<_Self&&>(__sender).__data_,
        *__sender.__context_,
        static_cast<_Rcvr&&>(__rcvr)};
    }
  };

  template <class _RcvrId>
  struct __read_operation {
    using _Receiver = stdexec::__t<_RcvrId>;

    struct __impl : __stoppable_op_base<_Receiver> {
      std::span<std::span<std::byte>> __buffers_;
      int __fd_;

      __impl(std::span<::iovec> __data, io_uring_context& __context, _Receiver&& __receiver)
        : __stoppable_op_base<_Receiver>{__context, static_cast<_Receiver&&>(__receiver)}
        , __buffers_{__data} {
      }

      static constexpr std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& __sqe) noexcept {
        ::io_uring_sqe __sqe_{};
        __sqe_.opcode = IORING_OP_READV;
        __sqe_.fd = __fd_;
        __sqe_.addr = bit_cast<__u64>(__buffers_.data());
        __sqe_.len = __buffers_.size();
      }

      void complete(const ::io_uring_cqe& __cqe) noexcept {
        if (__cqe.res >= 0) {
          stdexec::set_value(
            static_cast<_Receiver&&>(this->__receiver_), );
        } else {
          STDEXEC_ASSERT(__cqe.res < 0);
          stdexec::set_error(
            static_cast<_Receiver&&>(this->__receiver_),
            std::make_exception_ptr(std::system_error(-__cqe.res, std::system_category())));
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

    template <class _Receiver>
    using __operation_t =
      stdexec::__t<__open_operation<stdexec::__id<stdexec::__decay_t<_Receiver>>>>;

    io_uring_context* __context_;
    __open_data __data_;

    explicit __read_sender(io_uring_context& __context, __open_data __data) noexcept
      : __context_{&__context}
      , __data_{static_cast<__open_data&&>(__data)} {
    }

    template <__decays_to<__read_sender> _Self, class _Rcvr>
    friend auto tag_invoke(connect_t, _Self&& __sender, _Rcvr&& __rcvr) noexcept {
      return __operation_t<_Rcvr>{
        std::in_place,
        static_cast<_Self&&>(__sender).__data_,
        *__sender.__context_,
        static_cast<_Rcvr&&>(__rcvr)};
    }
  };

  template <class _RcvrId>
  struct __write_operation {
    using _Receiver = stdexec::__t<_RcvrId>;

    struct __impl : __stoppable_op_base<_Receiver> {
      std::span<std::span<std::byte>> __buffers_;
      int __fd_;

      __impl(std::span<::iovec> __data, io_uring_context& __context, _Receiver&& __receiver)
        : __stoppable_op_base<_Receiver>{__context, static_cast<_Receiver&&>(__receiver)}
        , __buffers_{__data} {
      }

      static constexpr std::false_type ready() noexcept {
        return {};
      }

      void submit(::io_uring_sqe& __sqe) noexcept {
        ::io_uring_sqe __sqe_{};
        __sqe_.opcode = IORING_OP_WRITEV;
        __sqe_.fd = __fd_;
        __sqe_.addr = bit_cast<__u64>(__buffers_.data());
        __sqe_.len = __buffers_.size();
      }

      void complete(const ::io_uring_cqe& __cqe) noexcept {
        if (__cqe.res >= 0) {
          stdexec::set_value(
            static_cast<_Receiver&&>(this->__receiver_), );
        } else {
          STDEXEC_ASSERT(__cqe.res < 0);
          stdexec::set_error(
            static_cast<_Receiver&&>(this->__receiver_),
            std::make_exception_ptr(std::system_error(-__cqe.res, std::system_category())));
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

    template <class _Receiver>
    using __operation_t =
      stdexec::__t<__open_operation<stdexec::__id<stdexec::__decay_t<_Receiver>>>>;

    io_uring_context* __context_;
    __open_data __data_;

    explicit __write_sender(io_uring_context& __context, __open_data __data) noexcept
      : __context_{&__context}
      , __data_{static_cast<__open_data&&>(__data)} {
    }

    template <__decays_to<__write_sender> _Self, class _Rcvr>
    friend auto tag_invoke(connect_t, _Self&& __sender, _Rcvr&& __rcvr) noexcept {
      return __operation_t<_Rcvr>{
        std::in_place,
        static_cast<_Self&&>(__sender).__data_,
        *__sender.__context_,
        static_cast<_Rcvr&&>(__rcvr)};
    }
  };

  struct __file_handle {
    native_fd_handle __native_;

    using buffer_type = std::span<std::byte>;
    using buffers_type = std::span<buffer_type>;
    using const_buffer_type = std::span<const std::byte>;
    using const_buffers_type = std::span<const_buffer_type>;
    using extent_type = ::off_t;

    friend __close_sender tag_invoke(async_resource::close_t, __file_handle __handle) noexcept {
      return {__handle.__native_.__context_, __handle.__native_.__fd_};
    }

    friend __write_sender tag_invoke(
      async::write_t,
      __file_handle __handle,
      const_buffers_type __data,
      extent_type __offset) noexcept;

    friend __read_sender tag_invoke(
      async::read_t,
      __file_handle __handle,
      buffers_type __data,
      extent_type __offset) noexcept;
  };

  struct __file {
    io_uring_context& __context_;
    __open_data __data_;

    explicit __file(
      io_uring_context& __context,
      std::filesystem::path __path,
      int __dirfd,
      int __flags,
      ::mode_t __mode) noexcept
      : __context_{__context}
      , __data_{static_cast<std::filesystem::path&&>(__path), __dirfd, __flags, __mode} {
    }

    friend __open_sender tag_invoke(exec::async_resource::open_t, const __file& __file) noexcept {
      return __open_sender{__file.__context_, __file.__data_};
    }
  };

  struct __io_scheduler {
    io_uring_context* __context_;

    using path_type = __file;
    using file_type = __file;

    template <same_as<async::file_t> _File, same_as<__io_scheduler> _Scheduler>
    friend file_type tag_invoke(_File, _Scheduler __self) noexcept {
      return file_type{
        *__self.__context_,
        __open_data{static_cast<std::filesystem::path&&>(__path), __dirfd, __flags, __mode}
      };
    }
  };
}}
