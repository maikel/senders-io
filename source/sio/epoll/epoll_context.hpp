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

#include <cstdint>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <optional>
#include <utility>

#include "exec/__detail/__atomic_intrusive_queue.hpp"
#include "exec/linux/safe_file_descriptor.hpp"
#include "exec/scope.hpp"
#include "stdexec/execution.hpp"
#include "stdexec/__detail/__execution_fwd.hpp"
#include "stdexec/stop_token.hpp"
#include "../intrusive_list.hpp"
#include "sio/assert.hpp"

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

    struct task_base {
      ~task_base() {
        SIO_ASSERT(!enqueued_);
      }

      std::atomic_bool enqueued_{false};
      task_base* next_{nullptr};
      void (*execute_)(task_base*) noexcept = nullptr;
    };

    using task_queue = stdexec::__intrusive_queue<&task_base::next_>;
    using atomic_task_queue = exec::__atomic_intrusive_queue<&task_base::next_>;

    class epoll_context : context_base {
     public:
      void run_until_stopped() {
        // Only one thread of execution is allowed to drive the io context.
        bool expected_running = false;

        if (
          !is_running_.compare_exchange_strong(expected_running, true, std::memory_order_relaxed)) {
          throw std::runtime_error("sio::epoll::epoll_context::run() called on a running context");
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

        task_queue_.append(requests_.pop_all());
        while (submitted_ > 0 || !task_queue_.empty()) {
          execute_tasks();
          if (submitted_ == 0 || (break_loop_.load(std::memory_order_acquire))) {
            break_loop_.store(false, std::memory_order_relaxed);
            break;
          }
          // TODO(xiaoming): timers
          submitted_ -= acquire_tasks_from_epoll();
          SIO_ASSERT(0 <= submitted_);
          task_queue_.append(requests_.pop_all());
        }

        SIO_ASSERT(submitted_ <= 1);
        if (stop_source_->stop_requested() && task_queue_.empty()) {
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
          task_queue_.append(requests_.pop_all());
          // TODO(xiaoming): complete tasks with stop
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


      // TODO(xiaoming)
      struct scheduler;
      constexpr scheduler get_scheduler() noexcept;

     private:
      // The thread that calls `context.run()` is called io thread, and other
      // threads are remote threads. This function checks which thread is using the
      // context.
      bool is_running_on_io_thread() const noexcept;

      // Check whether the thread submitting an operation to the context is the io
      // thread If the operation is submitted by the io thread, the operation is
      // submitted to the local queue; otherwise, the operation is submitted to
      // remote queue.
      void schedule_impl(task_base* op) noexcept;

      // Schedule the operation to the local queue.
      void schedule_local(task_base* op) noexcept;

      // Move all contents from remote queue to local queue.
      void schedule_local(task_queue ops) noexcept;

      // Schedule the operation to the remote queue.
      void schedule_remote(task_base* op) noexcept;

      /// \brief Submits the given task to the epoll_context.
      /// \returns true if the task was submitted, false if this io context and this task is have been stopped.
      bool submit(task_base* op) noexcept {
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
          // TODO(xiaoming): complete tasks with stop
          return false;
        } else {
          requests_.push_front(op);
          [[maybe_unused]] int prev = submissions_in_flight_.fetch_sub(
            1, std::memory_order_relaxed);
          SIO_ASSERT(prev > 0);
          return true;
        }
      }

      // Execute all tasks in the queue.
      // Tasks that were enqueued during the execution of already enqueued task won't be executed.
      // This bounds the amount of work to a finite amount.
      size_t execute_tasks() noexcept {
        if (task_queue_.empty()) {
          return 0;
        }
        size_t count = 0;
        auto tasks = static_cast<task_queue&&>(task_queue_);
        while (!tasks.empty()) {
          auto* task = tasks.pop_front();
          SIO_ASSERT(task->enqueued_);
          task->enqueued_ = false;
          std::exchange(task->next_, nullptr);
          task->execute_(task);
          ++count;
        }
        return count;
      }

      int acquire_tasks_from_epoll() {
        static constexpr int epoll_event_max_count = 256;
        epoll_event events[epoll_event_max_count];
        int timeout = task_queue_.empty() ? -1 : 0;
        int result = ::epoll_wait(epoll_fd_, events, epoll_event_max_count, timeout);
        throw_error_code_if(result < 0, errno);

        task_queue que;
        for (int i = 0; i < result; ++i) {
          if (events[i].data.ptr == &event_fd_) {
            return 0;
          } else if (events[i].data.ptr == &timer_fd_) {
            return 0;
          } else {
            auto& task = *reinterpret_cast<task_base*>(events[i].data.ptr);
            SIO_ASSERT(!task.enqueued_.load());
            task.enqueued_ = true;
            que.push_back(&task);
          }
        }
        schedule_local(static_cast<task_queue&&>(que));
        return result;
      }

      void wakeup() {
        uint64_t wakeup = 1;
        throw_error_code_if(::write(event_fd_, &wakeup, sizeof(uint64_t)) == -1, errno);
      }

      // This constant is used for submissions_in_flight to indicate that
      // no new submissions to this context will be completed by this context.
      static constexpr int no_new_submissions = -1;

      std::atomic<bool> is_running_{false};
      std::atomic<bool> break_loop_{false};
      std::atomic<int> submissions_in_flight_{0};
      uint64_t submitted_{0};
      task_queue task_queue_{};
      atomic_task_queue requests_{};
      std::optional<stdexec::in_place_stop_source> stop_source_{std::in_place};
    };

    // The scheduler with returned by `stdexec::get_schedule` customization point
    // object.
    class epoll_context::scheduler {
      // The envrionment of scheduler.
      struct schedule_env;

      // The timing operation which do an immediately schedule.
      template <typename ReceiverId>
      class schedule_op;

      // The timing operation which do a schedule at specific time point.
      template <typename ReceiverId>
      class schedule_at_op;

      // The sender returned by `stdexec::schedule` customization point object.
      class schedule_sender;

      // The sender returned by either `stdexec::schedule_at` or
      // `stdexec::schedule_after` customization point object.
      class schedule_at_sender;

      //                                                                                     [implement]
      struct schedule_env {
        // TODO: member cpo
        friend auto tag_invoke(
          stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
          const schedule_env& env) noexcept -> scheduler {
          return scheduler{env.context};
        }

        explicit constexpr schedule_env(epoll_context& ctx) noexcept
          : context(ctx) {
        }

        epoll_context& context;
      }; // schedule_env

      template <typename ReceiverId>
      class schedule_op {
        using receiver_t = stdexec::__t<ReceiverId>;

       public:
        struct __t : private task_base {
          using __id = schedule_op;
          using stop_token = stdexec::stop_token_of_t<stdexec::env_of_t<receiver_t>&>;

          constexpr __t(epoll_context& context, receiver_t r)
            : context_(context)
            , receiver_(static_cast<receiver_t&&>(r)) {
            execute_ = &execute_impl;
          }

          friend void tag_invoke(stdexec::start_t, __t& op) noexcept {
            op.start_impl();
          }

         private:
          constexpr void start_impl() noexcept {
            context_.schedule_impl(this);
          }

          static constexpr void execute_impl(task_base* p) noexcept {
            auto& self = *static_cast<__t*>(p);
            if constexpr (!std::unstoppable_token<stop_token>) {
              auto stop_token = stdexec::get_stop_token(stdexec::get_env(self.receiver_));
              if (stop_token.stop_requested()) {
                stdexec::set_stopped(static_cast<receiver_t&&>(self.receiver_));
                return;
              }
            }
            stdexec::set_value(static_cast<receiver_t&&>(self.receiver_));
          }

          friend scheduler::schedule_sender;

          epoll_context& context_;
          STDEXEC_NO_UNIQUE_ADDRESS receiver_t receiver_;
        };
      }; // schedule_op.

      class schedule_sender {
        template <typename Receiver>
        using op_t = stdexec::__t<schedule_op<stdexec::__id<Receiver>>>;

       public:
        struct __t {
          using is_sender = void;
          using __id = schedule_sender;
          using completion_signatures = stdexec::completion_signatures<
            stdexec::set_value_t(), //
            stdexec::set_stopped_t()>;

          template <typename Env>
          friend auto
            tag_invoke(stdexec::get_completion_signatures_t, const __t& self, Env&&) noexcept
            -> completion_signatures;

          friend auto tag_invoke(stdexec::get_env_t, const __t& self) noexcept -> schedule_env {
            return self.env_;
          }

          template <
            stdexec::__decays_to<__t> Sender,
            stdexec::receiver_of<completion_signatures> Receiver>
          friend auto tag_invoke(stdexec::connect_t, Sender&& self, Receiver receiver) noexcept
            -> op_t<Receiver> {
            return {static_cast<__t&&>(self).env_.context, static_cast<Receiver&&>(receiver)};
          }

          explicit constexpr __t(schedule_env env) noexcept
            : env_(env) {
          }

         private:
          friend epoll_context::scheduler;

          schedule_env env_;
        };
      }; // schedule_sender.


     public:
      explicit constexpr scheduler(epoll_context& context) noexcept
        : context_(&context) {
      }

      constexpr scheduler(const scheduler&) noexcept = default;

      constexpr scheduler& operator=(const scheduler&) = default;

      constexpr ~scheduler() = default;

      friend auto tag_invoke(stdexec::schedule_t, const scheduler& sched) noexcept
        -> stdexec::__t<schedule_sender> {
        return stdexec::__t<schedule_sender>{schedule_env{*sched.context_}};
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

    inline constexpr epoll_context::scheduler epoll_context::get_scheduler() noexcept {
      return scheduler{*this};
    }

    // !!!Stores the address of the context owned by the current thread
    static thread_local epoll_context* current_thread_context;

    inline bool epoll_context::is_running_on_io_thread() const noexcept {
      return this == current_thread_context;
    }

    inline void epoll_context::schedule_impl(task_base* op) noexcept {
      assert(op != nullptr);
      if (is_running_on_io_thread()) {
        schedule_local(op);
      } else {
        schedule_remote(op);
      }
    }

    inline void epoll_context::schedule_local(task_base* op) noexcept {
      assert(op->execute_ != nullptr);
      assert(!op->enqueued_);
      op->enqueued_ = true;
      task_queue_.push_back(op);
    }

    inline void epoll_context::schedule_local(task_queue ops) noexcept {
      // Do not adjust the enqueued flag, which is still true because the ops will
      // immediately be transferred from the remote queue to the local queue.
      task_queue_.append(std::move(ops));
    }

    inline void epoll_context::schedule_remote(task_base* op) noexcept {
    }


  }; // namespace epoll

  using epoll_context = epoll::epoll_context;
} // namespace sio
