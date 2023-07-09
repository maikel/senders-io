/*
 * Copyright (c) 2023 Xiaoming Zhang
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

#include <cerrno>
#include <cstdint>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <optional>
#include <system_error>
#include <utility>

#include "exec/__detail/__atomic_intrusive_queue.hpp"
#include "exec/linux/safe_file_descriptor.hpp"
#include "exec/scope.hpp"
#include "stdexec/execution.hpp"
#include "stdexec/__detail/__execution_fwd.hpp"
#include "stdexec/stop_token.hpp"
#include "../intrusive_heap.hpp"
#include "sio/assert.hpp"
#include "exec/__detail/__manual_lifetime.hpp"

namespace sio {
  namespace epoll {
    inline void throw_error_code_if(bool cond, int ec) {
      if (cond) {
        throw std::system_error(ec, std::system_category());
      }
    }

    inline int create_epoll() {
      static constexpr int epoll_create_size_hint = 1024;
      int fd = ::epoll_create(epoll_create_size_hint);
      throw_error_code_if(fd < 0, errno);
      return fd;
    }

    inline int create_eventfd() {
      int fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
      throw_error_code_if(fd < 0, errno);
      return fd;
    }

    inline int create_timer() {
      int fd = ::timerfd_create(CLOCK_MONOTONIC, 0);
      throw_error_code_if(fd < 0, errno);
      return fd;
    }

    struct context_base : stdexec::__immovable {
      context_base()
        : epoll_fd_(create_epoll())
        , event_fd_(create_eventfd())
        , timer_fd_(create_timer()) {
        epoll_event event_ev = {
          .events = EPOLLIN | EPOLLERR | EPOLLET, .data = {.ptr = &event_fd_}};
        int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event_ev);
        throw_error_code_if(ret < 0, errno);

        epoll_event timer_ev = {.events = EPOLLIN | EPOLLERR, .data = {.ptr = &timer_fd_}};
        ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &timer_ev);
        throw_error_code_if(ret < 0, errno);
      }

      ~context_base() {
        epoll_event event = {};
        int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, event_fd_, &event);
        throw_error_code_if(ret < 0, errno);

        event = {};
        ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, timer_fd_, &event);
        throw_error_code_if(ret < 0, errno);
      }

      exec::safe_file_descriptor epoll_fd_;
      exec::safe_file_descriptor event_fd_;
      exec::safe_file_descriptor timer_fd_;
    };

    struct operation_base;

    struct operation_vtable {
      bool (*ready_)(operation_base*) noexcept = nullptr;   // NOLINT
      void (*execute_)(operation_base*) noexcept = nullptr; // NOLINT
      void (*complete_)(operation_base*, const std::error_code&) noexcept = nullptr;
    };

    struct operation_base : stdexec::__immovable {
      const operation_vtable* vtable_;
      operation_base* next_{nullptr};
      std::atomic_bool enqueued_{false};

      explicit operation_base(const operation_vtable& vtable)
        : vtable_{&vtable} {
      }
    };

    inline void stop_this_operation(operation_base* op) noexcept {
      op->vtable_->complete_(op, std::make_error_code(std::errc::operation_canceled));
    }

    inline void complete_this_operation(operation_base* op) noexcept {
      op->vtable_->complete_(op, std::error_code{});
    }

    inline void execute_this_operation(operation_base* op) noexcept {
      op->vtable_->execute_(op);
    }

    using time_point = std::chrono::time_point<std::chrono::steady_clock>;
    class epoll_context;

    struct schedule_at_base_task : operation_base {
      static constexpr uint32_t timer_elapsed = 1;
      static constexpr uint32_t cancel_pending = 2;

      schedule_at_base_task* timer_next_{nullptr};
      schedule_at_base_task* timer_prev_{nullptr};
      epoll_context& context_;
      time_point due_time_{};
      bool can_be_cancelled_{true};
      std::atomic<uint32_t> state_{0};
    };

    using operation_queue = stdexec::__intrusive_queue<&operation_base::next_>;
    using atomic_task_queue = exec::__atomic_intrusive_queue<&operation_base::next_>;
    using timer_heap = intrusive_heap<
      schedule_at_base_task,
      &schedule_at_base_task::timer_next_,
      &schedule_at_base_task::timer_prev_,
      time_point,
      &schedule_at_base_task::due_time_>;

    class scheduler;

    class epoll_context : context_base {
     public:
      void run_until_stopped() {
        // Only one thread of execution is allowed to drive the io context.
        bool expected_running = false;

        if (
          !is_running_.compare_exchange_strong(expected_running, true, std::memory_order_relaxed)) {
          throw std::runtime_error("sio::epoll::context::run() called on a running context");
        } else {
          // Check whether we restart the context after a context-wide stop.
          // We have to reset the stop source in this case.
          int in_flight = submissions_in_flight_.load(std::memory_order_relaxed);
          if (in_flight == no_new_submissions) {
            stop_source_.emplace();
            // Make emplacement of stop source visible to other threads
            // and open the door for new submissions.
            submissions_in_flight_.store(0, std::memory_order_release);
          }
        }

        exec::scope_guard set_not_running{[&]() noexcept {
          is_running_.store(false, std::memory_order_relaxed);
        }};

        op_queue_.append(requests_.pop_all());
        while (submitted_ > 0 || !op_queue_.empty()) {
          execute_operations();
          if (submitted_ == 0 || (break_loop_.load(std::memory_order_acquire))) {
            break_loop_.store(false, std::memory_order_relaxed);
            break;
          }
          // TODO(xiaoming): timers
          submitted_ -= acquire_operations_from_epoll();
          SIO_ASSERT(0 <= submitted_);
          op_queue_.append(requests_.pop_all());
        }

        SIO_ASSERT(submitted_ <= 1);
        if (stop_source_->stop_requested() && op_queue_.empty()) {
          SIO_ASSERT(submitted_ == 0);
          // try to shutdown the request queue
          int in_flight_expected = 0;
          while (submissions_in_flight_.compare_exchange_weak(
            in_flight_expected, no_new_submissions, std::memory_order_relaxed)) {
            if (in_flight_expected == no_new_submissions) {
              break;
            }
            in_flight_expected = 0;
          }
          SIO_ASSERT(submissions_in_flight_.load(std::memory_order_relaxed) == no_new_submissions);
          // There could have been requests in flight.
          // Complete all of them and then stop it, finally.
          op_queue_.append(requests_.pop_all());
          // TODO(xiaoming): complete op_queue with stop
        }
      }

      // Request to stop the context.
      // Note that the context may block on the epoll_wait call,
      // so we need to wake up the context.
      void request_stop() {
        stop_source_->request_stop();
        wakeup();
      }

      bool stop_requested() const noexcept {
        return stop_source_->stop_requested();
      }

      stdexec::in_place_stop_token get_stop_token() const noexcept {
        return stop_source_->get_token();
      }

      bool is_running() const noexcept {
        return is_running_.load(std::memory_order_relaxed);
      }

      /// @brief  Breaks out of the run loop of the io context without stopping the context.
      void finish() {
        break_loop_.store(true, std::memory_order_release);
        wakeup();
      }

      scheduler get_scheduler() noexcept;

      /// \brief schedule the given op to the context.
      /// \returns true if the op was scheduled, false if this io context and this op is have been stopped.
      bool schedule(operation_base* op) noexcept {
        // As long as the number of in-flight submissions is not no_new_submissions, we can
        // increment the counter and push the operation onto the queue.
        // If the number of in-flight submissions is no_new_submissions, we have already
        // finished the stop operation of the io context and we can immediately stop the operation inline.
        // Remark: As long as the stopping is in progress we can still submit new operations.
        // But no operation will be submitted to io uring unless it is a cancellation operation.
        int n = 0;
        while (n != no_new_submissions
               && !submissions_in_flight_.compare_exchange_weak(
                 n, n + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
        }

        if (n == no_new_submissions) {
          stop_this_operation(op);
          return false;
        } else {
          requests_.push_front(op);
          [[maybe_unused]] int prev = submissions_in_flight_.fetch_sub(
            1, std::memory_order_relaxed);
          SIO_ASSERT(prev > 0);
          return true;
        }
      }

      void wakeup() {
        uint64_t wakeup = 1;
        throw_error_code_if(::write(event_fd_, &wakeup, sizeof(uint64_t)) == -1, errno);
      }

      int epoll_fd() {
        return epoll_fd_;
      }

     private:
      // Execute all op_queue in the queue.
      // Tasks that were enqueued during the execution of already enqueued op won't be executed.
      // This bounds the amount of work to a finite amount.
      size_t execute_operations() noexcept {
        if (op_queue_.empty()) {
          return 0;
        }
        size_t count = 0;
        auto op_queue = static_cast<operation_queue&&>(op_queue_);
        while (!op_queue.empty()) {
          auto* op = op_queue.pop_front();
          SIO_ASSERT(op->enqueued_);
          op->enqueued_ = false;
          std::exchange(op->next_, nullptr);
          if (!op->vtable_->ready_(op)) {
            execute_this_operation(op);
          }
          complete_this_operation(op);
          ++count;
        }
        return count;
      }

      int acquire_operations_from_epoll() {
        static constexpr int epoll_event_max_count = 256;
        epoll_event events[epoll_event_max_count];
        int timeout = op_queue_.empty() ? -1 : 0;
        int result = ::epoll_wait(epoll_fd_, events, epoll_event_max_count, timeout);
        throw_error_code_if(result < 0, errno);

        operation_queue que;
        for (int i = 0; i < result; ++i) {
          if (events[i].data.ptr == &event_fd_) {
            return 0;
          } else if (events[i].data.ptr == &timer_fd_) {
            return 0;
          } else {
            auto& op = *reinterpret_cast<operation_base*>(events[i].data.ptr);
            SIO_ASSERT(!op.enqueued_.load());
            op.enqueued_ = true;
            que.push_back(&op);
          }
        }
        op_queue_.append(static_cast<operation_queue&&>(que));
        return result;
      }

      // This constant is used for submissions_in_flight to indicate that
      // no new submissions to this context will be completed by this context.
      static constexpr int no_new_submissions = -1;

      std::atomic<bool> is_running_{false};
      std::atomic<bool> break_loop_{false};
      std::atomic<int> submissions_in_flight_{0};
      uint64_t submitted_{0};
      operation_queue op_queue_{};
      atomic_task_queue requests_{};
      std::optional<stdexec::in_place_stop_source> stop_source_{std::in_place};
    };

    template <class Operation>
    concept io_operation = requires(Operation& op, const std::error_code& ec) {
                             { op.context() } noexcept -> std::convertible_to<epoll_context&>;
                             { op.ready() } noexcept -> std::convertible_to<bool>;
                             { op.execute() } noexcept;
                             { op.complete(ec) } noexcept;
                           };

    template <class Operation>
    concept stoppable_operation =
      io_operation<Operation>
      && requires(Operation& op) {
           {
             static_cast<Operation&&>(op).receiver()
             } noexcept
               -> stdexec::receiver_of< stdexec::completion_signatures<stdexec::set_stopped_t()>>;
         };

    template <stoppable_operation Operation>
    using receiver_of_t = stdexec::__decay_t<decltype(std::declval<Operation&>().receiver())>;

    template <io_operation Base>
    struct io_operation_facade : operation_base {
      static bool ready(operation_base* op) noexcept {
        auto self = static_cast<io_operation_facade*>(op);
        return self->base_.ready();
      }

      static void execute(operation_base* op) noexcept {
        auto self = static_cast<io_operation_facade*>(op);
        self->base_.execute();
      }

      static void complete(operation_base* op, const std::error_code& ec) noexcept {
        auto self = static_cast<io_operation_facade*>(op);
        self->base_.complete(ec);
      }

      static constexpr operation_vtable vtable{&ready, &execute, &complete};

      template <class... Args>
        requires stdexec::constructible_from<Base, std::in_place_t, Args...>
      explicit io_operation_facade(std::in_place_t, Args&&... args) noexcept(
        stdexec::__nothrow_constructible_from<Base, Args...>)
        : operation_base{vtable}
        , base_(std::in_place, static_cast<Args&&>(args)...) {
      }

      template <class... Args>
        requires stdexec::constructible_from<Base, Args...>
      explicit io_operation_facade(std::in_place_t, Args&&... args) noexcept(
        stdexec::__nothrow_constructible_from<Base, Args...>)
        : operation_base{vtable}
        , base_(static_cast<Args&&>(args)...) {
      }

      Base& base() noexcept {
        return base_;
      }

     private:
      Base base_;

      STDEXEC_CPO_ACCESS(stdexec::start_t);

      STDEXEC_DEFINE_CUSTOM(void start)(this io_operation_facade& self, stdexec::start_t) noexcept {
        epoll_context& ctx = self.base_.context();
        if (ctx.schedule(&self)) {
          ctx.wakeup();
        }
      }
    };

    template <class ReceiverId>
    struct schedule_operation {
      using Receiver = stdexec::__t<ReceiverId>;

      struct impl {
        epoll_context& ctx_;
        STDEXEC_NO_UNIQUE_ADDRESS Receiver rcvr_;

        impl(epoll_context& ctx, Receiver&& rcvr)
          : ctx_{ctx}
          , rcvr_{static_cast<Receiver&&>(rcvr)} {
        }

        epoll_context& context() const noexcept {
          return ctx_;
        }

        static constexpr std::true_type ready() noexcept {
          return {};
        }

        static constexpr void execute() noexcept {
        }

        void complete(const std::error_code& ec) noexcept {
          auto token = stdexec::get_stop_token(stdexec::get_env(rcvr_));
          if (
            ec == std::errc::operation_canceled || ctx_.stop_requested()
            || token.stop_requested()) {
            stdexec::set_stopped(static_cast<Receiver&&>(rcvr_));
          } else {
            stdexec::set_value(static_cast<Receiver&&>(rcvr_));
          }
        }
      };

      using __t = io_operation_facade<impl>;
    };

    template <class Base>
    struct stop_operation : operation_base {
      Base* base_;

      static constexpr bool ready(operation_base*) noexcept {
        return false;
      }

      static void execute(operation_base* op) noexcept {
        // TODO(xiaoming)
        // auto self = static_cast<stop_operation*>(op);
        // self->base_->execute_stop();
      }

      static void complete(operation_base* op, const std::error_code& ec) noexcept {
        auto self = static_cast<stop_operation*>(op);
        if (self->base_->ops_cnt_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          self->base_->on_context_stop_.reset();
          self->base_->on_receiver_stop_.reset();
          stdexec::set_stopped((static_cast<Base&&>(*self->base_)).receiver());
        }
      }

      static constexpr operation_vtable vtable{&ready, &execute, &complete};

      explicit stop_operation(Base* base) noexcept
        : operation_base(vtable)
        , base_{base} {
      }

      void start() noexcept {
        int expected = 1;
        if (base_->ops_cnt_.compare_exchange_strong(expected, 2, std::memory_order_relaxed)) {
          if (base_->context().submit(this)) {
            base_->context().wakeup();
          }
        }
      };
    };

    template <stoppable_operation Base>
    struct stoppable_operation_facade {
      using Receiver = receiver_of_t<Base>;

      struct impl {
        struct stop_callback {
          impl* self_;

          void operator()() noexcept {
            self_->stop_operation_.start();
          }
        };

        using on_context_stop_t = std::optional<stdexec::in_place_stop_callback<stop_callback>>;
        using on_receiver_stop_t = std::optional<typename stdexec::stop_token_of_t<
          stdexec::env_of_t<Receiver>&>::template callback_type<stop_callback>>;

        on_context_stop_t on_context_stop_{};
        on_receiver_stop_t on_receiver_stop_{};
        std::atomic<int> ops_cnt_{0};
        stop_operation<impl> stop_operation_;
        Base base_;

        template <class... Args>
          requires stdexec::constructible_from<Base, Args...>
        impl(std::in_place_t, operation_base* parent, Args&&... args) noexcept(
          stdexec::__nothrow_constructible_from<Base, Args...>)
          : base_(static_cast<Args&&>(args)...)
          , stop_operation_{this} {
        }

        epoll_context& context() noexcept {
          return this->base_.context();
        }

        Receiver& receiver() & noexcept {
          return this->base_.receiver();
        }

        Receiver&& receiver() && noexcept {
          return (Receiver &&) this->base_.receiver();
        }

        bool ready() const noexcept {
          return this->base_.ready();
        }

        void execute() noexcept {
          [[maybe_unused]] int prev = ops_cnt_.fetch_add(1, std::memory_order_relaxed);
          STDEXEC_ASSERT(prev == 0);
          epoll_context& ctx_ = this->base_.context();
          Receiver& rcvr = this->base_.receiver();
          on_context_stop_.emplace(ctx_.get_stop_token(), stop_callback{this});
          on_receiver_stop_.emplace(
            stdexec::get_stop_token(stdexec::get_env(rcvr)), stop_callback{this});
          this->base_.execute();
        }

        void complete(const std::error_code& ec) noexcept {
          if (ops_cnt_.fetch_sub(1, std::memory_order_relaxed) == 1) {
            on_context_stop_.reset();
            on_receiver_stop_.reset();
            Receiver& rcvr = this->base_.receiver();
            epoll_context& ctx_ = this->base_.context();
            auto token = stdexec::get_stop_token(stdexec::get_env(rcvr));
            if (
              ec == std::errc::operation_canceled || ctx_.stop_requested()
              || token.stop_requested()) {
              stdexec::set_stopped(static_cast<Receiver&&>(rcvr));
            } else {
              this->base_.complete(ec);
            }
          }
        }
      };

      using __t = io_operation_facade<impl>;
    };

    template <class Base>
    using stoppable_task_facade_t = stdexec::__t<stoppable_operation_facade<Base>>;

    enum class socket_op_type {
      op_read = 1,
      op_accept = 1,
      op_write = 2,
      op_connect = 2
    };

    template <io_operation Base>
    struct socket_operation_facade {
      struct impl : operation_base {
        Base base_;

        bool add_events() {
          epoll_event event{.data = {.ptr = this}};
          if (base_.op_type_ == socket_op_type::op_read) {
            event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET;
          } else {
            event.events = EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET;
          }
          if (::epoll_ctl(base_.context().epoll_fd(), EPOLL_CTL_ADD, base_.fd_, &event) == -1) {
            base_.ec_ = std::error_code{errno, std::system_category()};
            return false;
          }
          return true;
        }

        bool remove_events() {
          epoll_event event = {};
          if (::epoll_ctl(base_.context().epoll_fd(), EPOLL_CTL_DEL, base_.fd_, &event) == -1) {
            base_.ec_ = std::error_code{errno, std::system_category()};
            return false;
          }
          return true;
        }

        static constexpr bool ready(operation_base*) noexcept {
          return false;
        }

        static void execute(operation_base* op) noexcept {
          auto self = static_cast<impl*>(op);
          self->base_.execute(self);
          if (
            self->base_.ec_ == std::errc::resource_unavailable_try_again
            || self->base_.ec_ == std::errc::operation_would_block) {
            self->base_.ec_.clear();
            self->vtable_.execute_ = wakeup;
            self->add_events();
          }
        }

        static void complete(operation_base* op, const std::error_code& ec) noexcept {
          auto self = static_cast<impl*>(op);
          return self->base_.complete(ec);
        }

        static void wakeup(operation_base* op) noexcept {
          assert(op->enqueued_.load() == false);
          auto self = static_cast<socket_operation_facade*>(op);
          self->remove_events();
          self->base_.execute();
        }

        static constexpr operation_vtable vtable{&ready, &execute, &complete};

        explicit impl(Base base)
          : operation_base(vtable)
          , base_(base) {
        }
      };

      using __t = stoppable_operation_facade<impl>;
    };

    class scheduler {
     public:
      struct schedule_env {
        epoll_context* context_;

        friend auto tag_invoke(
          stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
          const schedule_env& env) noexcept -> scheduler {
          return scheduler{*env.context_};
        }
      };

      class schedule_sender {
        template <class Receiver>
        using op_t = stdexec::__t<schedule_operation<stdexec::__id<Receiver>>>;

       public:
        struct __t {
          using is_sender = void;
          using __id = schedule_sender;
          using completion_signatures =
            stdexec::completion_signatures< stdexec::set_value_t(), stdexec::set_stopped_t()>;

          template <class Env>
          friend auto
            tag_invoke(stdexec::get_completion_signatures_t, const __t& self, Env) noexcept
            -> completion_signatures;

          friend auto tag_invoke(stdexec::get_env_t, const __t& self) noexcept -> schedule_env {
            return self.env_;
          }

          template <
            stdexec::__decays_to<__t> Sender,
            stdexec::receiver_of<completion_signatures> Receiver>
          friend auto tag_invoke(stdexec::connect_t, Sender&& self, Receiver receiver) noexcept
            -> op_t<Receiver> {
            return {static_cast<__t&&>(self).env_.context_, static_cast<Receiver&&>(receiver)};
          }

          explicit __t(schedule_env env) noexcept
            : env_(env) {
          }

         private:
          friend scheduler;

          schedule_env env_;
        };
      }; // schedule_sender.


     public:
      explicit scheduler(epoll_context& context) noexcept
        : context_(&context) {
      }

      scheduler(const scheduler&) noexcept = default;

      scheduler& operator=(const scheduler&) = default;

      ~scheduler() = default;

      friend auto tag_invoke(stdexec::schedule_t, const scheduler& sched) noexcept
        -> stdexec::__t<schedule_sender> {
        return stdexec::__t<schedule_sender>{schedule_env{sched.context_}};
      }

     private:
      friend bool operator==(scheduler a, scheduler b) noexcept {
        return a.context_ == b.context_;
      }

      friend bool operator!=(scheduler a, scheduler b) noexcept {
        return a.context_ != b.context_;
      }

      friend epoll_context;

      epoll_context* context_;
    };

    inline scheduler epoll_context::get_scheduler() noexcept {
      return scheduler{*this};
    }

  }; // namespace epoll

  using epoll_context = epoll::epoll_context;
} // namespace sio
