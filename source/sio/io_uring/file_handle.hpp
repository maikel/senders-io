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
#include "sio/ip/endpoint.hpp"

#include <exception>
#include <filesystem>
#include <span>

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

namespace sio::io_uring {
  using exec::__io_uring::__task;
  using exec::__io_uring::__task_vtable;

  template <class Op>
  concept io_task = //
    requires(Op& op, ::io_uring_sqe& sqe, const ::io_uring_cqe& cqe) {
      { op.context() } noexcept;
      { op.ready() } noexcept -> std::convertible_to<bool>;
      { op.submit(sqe) } noexcept;
      { op.complete(cqe) } noexcept;
    };

  template <class Op>
  concept stoppable_task = //
    io_task<Op> &&         //
    requires(Op& op) {
      {
        ((Op&&) op).receiver()
      } noexcept -> stdexec::receiver_of<stdexec::completion_signatures<stdexec::set_stopped_t()>>;
    };

  template <stoppable_task Op>
  using stoppable_task_receiver_of_t = stdexec::__decay_t<decltype(std::declval<Op&>().receiver())>;

  template <io_task Base>
  struct io_task_facade : __task {
    static bool ready_(__task* pointer) noexcept {
      io_task_facade* self = static_cast<io_task_facade*>(pointer);
      return self->base_.ready();
    }

    static void submit_(__task* pointer, ::io_uring_sqe& sqe) noexcept {
      io_task_facade* self = static_cast<io_task_facade*>(pointer);
      self->base_.submit(sqe);
    }

    static void complete_(__task* pointer, const ::io_uring_cqe& cqe) noexcept {
      io_task_facade* self = static_cast<io_task_facade*>(pointer);
      self->base_.complete(cqe);
    }

    static constexpr __task_vtable vtable{&ready_, &submit_, &complete_};

    template <class... Args>
      requires stdexec::constructible_from<Base, std::in_place_t, __task*, Args...>
    io_task_facade(std::in_place_t, Args&&... args) noexcept(
      nothrow_constructible_from<Base, __task*, Args...>)
      : __task{vtable}
      , base_(std::in_place, static_cast<__task*>(this), static_cast<Args&&>(args)...) {
    }

    template <class... Args>
      requires stdexec::constructible_from<Base, Args...>
    io_task_facade(std::in_place_t, Args&&... args) noexcept(
      nothrow_constructible_from<Base, Args...>)
      : __task{vtable}
      , base_(static_cast<Args&&>(args)...) {
    }

    Base& base() noexcept {
      return base_;
    }

   private:
    Base base_;

    STDEXEC_CPO_ACCESS(stdexec::start_t);

    STDEXEC_DEFINE_CUSTOM(void start)(this io_task_facade& self, stdexec::start_t) noexcept {
      auto& context = self.base_.context();
      if (context.submit(&self)) {
        context.wakeup();
      }
    }
  };

  template <class Base>
  struct stop_operation {
    class type : public __task {
      Base* op_;
     public:
      static bool ready_(__task*) noexcept {
        return false;
      }

      static void submit_(__task* pointer, ::io_uring_sqe& sqe) noexcept {
        type* self = static_cast<type*>(pointer);
        self->submit(sqe);
      }

      static void complete_(__task* pointer, const ::io_uring_cqe& cqe) noexcept {
        type* self = static_cast<type*>(pointer);
        self->complete(cqe);
      }

      void submit(::io_uring_sqe& sqe) noexcept {
#ifdef STDEXEC_HAS_IO_URING_ASYNC_CANCELLATION
        if constexpr (requires(Base* op, ::io_uring_sqe& sqe) { op->submit_stop(sqe); }) {
          op_->submit_stop(sqe);
        } else {
          sqe = ::io_uring_sqe{
            .opcode = IORING_OP_ASYNC_CANCEL,          //
            .addr = std::bit_cast<__u64>(op_->parent_) //
          };
        }
#else
        op_->submit_stop(sqe);
#endif
      }

      void complete(const ::io_uring_cqe&) noexcept {
        if (op_->n_ops_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          op_->on_context_stop_.reset();
          op_->on_receiver_stop_.reset();
          stdexec::set_stopped(((Base&&) *op_).receiver());
        }
      }

      static constexpr __task_vtable vtable{&ready_, &submit_, &complete_};

      explicit type(Base* op) noexcept
        : __task(vtable)
        , op_{op} {
      }

      void start() noexcept {
        int expected = 1;
        if (op_->n_ops_.compare_exchange_strong(expected, 2, std::memory_order_relaxed)) {
          if (op_->context().submit(this)) {
            op_->context().wakeup();
          }
        }
      }
    };
  };

  template <class Base, bool False>
  struct impl_base {
    __task* parent_;
    Base base_;

    template <class... Args>
    impl_base(__task* parent, std::in_place_t, Args&&... args) noexcept(
      nothrow_constructible_from<Base, Args...>)
      : parent_{parent}
      , base_((Args&&) args...) {
    }
  };

  template <class Base>
  struct impl_base<Base, true> {
    __task* parent_;
    Base base_;

    template <class... Args>
    impl_base(__task* parent, std::in_place_t, Args&&... args) noexcept(
      nothrow_constructible_from<Base, Args...>)
      : parent_{parent}
      , base_((Args&&) args...) {
    }

    void submit_stop(::io_uring_sqe& sqe) noexcept {
      base_.submit_stop(sqe);
    }
  };

  template <stoppable_task Base>
  struct stoppable_task_facade {
    using Receiver = stoppable_task_receiver_of_t<Base>;
    using Context = decltype(std::declval<Base&>().context());

    template <class Ty>
    static constexpr bool has_submit_stop_v = requires(Ty& base, ::io_uring_sqe& sqe) {
      base.submit_stop(sqe);
    };

    using base_t = impl_base<Base, has_submit_stop_v<Base>>;

    struct impl : base_t {
      struct stop_callback {
        impl* self_;

        void operator()() noexcept {
          self_->stop_operation_.start();
        }
      };

      using on_context_stop_t = std::optional<stdexec::in_place_stop_callback<stop_callback>>;
      using on_receiver_stop_t = std::optional<typename stdexec::stop_token_of_t<
        stdexec::env_of_t<Receiver>&>::template callback_type<stop_callback>>;

      typename stop_operation<impl>::type stop_operation_;
      std::atomic<int> n_ops_{0};
      on_context_stop_t on_context_stop_{};
      on_receiver_stop_t on_receiver_stop_{};

      template <class... Args>
        requires constructible_from<Base, Args...>
      impl(std::in_place_t, __task* parent, Args&&... args) noexcept(
        nothrow_constructible_from<Base, Args...>)
        : base_t(parent, std::in_place, (Args&&) args...)
        , stop_operation_{this} {
      }

      Context& context() noexcept {
        return this->base_.context();
      }

      Receiver& receiver() & noexcept {
        return this->base_.receiver();
      }

      Receiver&& receiver() && noexcept {
        return (Receiver&&) this->base_.receiver();
      }

      bool ready() const noexcept {
        return this->base_.ready();
      }

      void submit(::io_uring_sqe& sqe) noexcept {
        [[maybe_unused]] int prev = n_ops_.fetch_add(1, std::memory_order_relaxed);
        STDEXEC_ASSERT(prev == 0);
        Context& context = this->base_.context();
        Receiver& receiver = this->base_.receiver();
        on_context_stop_.emplace(context.get_stop_token(), stop_callback{this});
        on_receiver_stop_.emplace(
          stdexec::get_stop_token(stdexec::get_env(receiver)), stop_callback{this});
        this->base_.submit(sqe);
      }

      void complete(const ::io_uring_cqe& cqe) noexcept {
        if (n_ops_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          on_context_stop_.reset();
          on_receiver_stop_.reset();
          Receiver& receiver = this->base_.receiver();
          Context& context = this->base_.context();
          auto token = stdexec::get_stop_token(stdexec::get_env(receiver));
          if (cqe.res == -ECANCELED || context.stop_requested() || token.stop_requested()) {
            stdexec::set_stopped((Receiver&&) receiver);
          } else {
            this->base_.complete(cqe);
          }
        }
      }
    };

    using type = io_task_facade<impl>;
  };

  template <class Base>
  using stoppable_task_facade_t = typename stoppable_task_facade<Base>::type;

  template <class Context, class Receiver>
  struct stoppable_op_base {
    Context& context_;
    Receiver receiver_;

    Receiver& receiver() & noexcept {
      return receiver_;
    }

    Receiver&& receiver() && noexcept {
      return static_cast<Receiver&&>(receiver_);
    }

    Context& context() noexcept {
      return context_;
    }
  };

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