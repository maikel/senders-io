/*
 * Copyright (c) 2023 Runner-2019
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

#include <sys/epoll.h>
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
    constexpr int epoll_create_size_hint = 1024;

    inline void throw_error_code_if(bool cond, int ec) {
      if (cond) {
        throw std::system_error(ec, std::system_category());
      }
    }

    inline int epoll_create(int hint) {
      int fd = ::epoll_create(hint);
      throw_error_code_if(fd < 0, errno);
      return fd;
    }

    struct context_base : stdexec::__immovable {
      context_base()
        : epoll_fd_(epoll_create(epoll_create_size_hint)) {
      }

      exec::safe_file_descriptor epoll_fd_;
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
          throw std::runtime_error("epoll_context::run() called on a running context");
        } else {
          // Check whether we restart the context after a context-wide stop.
          // We have to reset the stop source in this case.
          int in_flight = n_submissions_in_flight_.load(std::memory_order_relaxed);
          if (in_flight == no_new_submissions) {
            stop_source_.emplace();
            // Make emplacement of stop source visible to other threads
            // and open the door for new submissions.
            n_submissions_in_flight_.store(0, std::memory_order_release);
          }
        }

        exec::scope_guard set_not_running{[&]() noexcept {
          is_running_.store(false, std::memory_order_relaxed);
        }};

        size_t executed_cnt = 0;
        while (true) { // TODO: still in progress
          executed_cnt = execute_tasks();
          if (stop_source_->stop_requested()) {
            break;
          }
          // if (timers_are_dirty_) {
          //   update_timers();
          // }
          if (!processed_outstanding_submitted_) {
            // This flag will be true if we have collected some items from remote
            // queue to local queue. So we have cleared remote queue. Just wait the
            // next time's interrupt.
            processed_outstanding_submitted_ = try_schedule_remote_to_local();
          }
          acquire_completion_queue_items();
        }
      }

      epoll_context()
        : context_base()
        , processed_outstanding_submitted_(false)
        , timer_fd_(create_timer())
        // , interrupter_()
        // , timers_()
        // , current_earliest_due_time_()
        // , timers_are_dirty_(false)
        , task_queue_()
        , requests_()
        , stop_source_(std::in_place)
        , is_running_(false) {
        add_timer_to_epoll();
        add_interrupter_to_epoll();
      }

      // Destructor.
      ~epoll_context() {
        remove_timer_from_epoll();
        remove_interrupter_from_epoll();
      }

      // Request to stop the context. Note that the context may block on the
      // epoll_wait call, so we must use interrupt to wake up the context.
      void request_stop() {
        stop_source_->request_stop();
        // interrupter_.interrupt();
      }

      // Whether this context have been request to stop.
      bool stop_requested() const noexcept {
        return stop_source_->stop_requested();
      }

      // Get this context associated stop token.
      stdexec::in_place_stop_token get_stop_token() const noexcept {
        return stop_source_->get_token();
      }

      // Check whether this context is running.
      bool is_running() const noexcept {
        return is_running_.load(std::memory_order_relaxed);
      }

      // TODO
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
          assert(task->enqueued_);
          task->enqueued_ = false;
          std::exchange(task->next_, nullptr);
          task->execute_(task);
          ++count;
        }
        return count;
      }

      // Check if any completion queue items are available and if so add them to the
      // local queue.
      void acquire_completion_queue_items();

      // Collect the contents of the remote queue and pass them to local queue.
      // The return value represents whether the remote queue is empty before we
      // make a collection. Returns true means remote queue is emtpy before we
      // collect.
      bool try_schedule_remote_to_local() noexcept;

      // Signal the remote queue eventfd.
      // This should only be called after trying to enqueue() work to the remote
      // queue and being told that the I/O thread is inactive.
      void interrupt() {
        // interrupter_.interrupt();
      }

      // Create epoll file descriptor. Throws an error when creation fails.
      int create_epoll();

      // Create timer file descriptor. Throws an error when creation fails.
      int create_timer();

      // Add the timer's file descriptor to epoll. Throws an error when
      // registrations fails.
      void add_timer_to_epoll();

      // Remove the timer's file descriptor from epoll. Throws an error when remove
      // fails.
      void remove_timer_from_epoll();

      // Add the interrupter's file descriptor to epoll.  Throws an error when
      // registrations fails.
      void add_interrupter_to_epoll();

      // Remove the interrupter's file descriptor from epoll. Throws an error when
      // remove fails.
      void remove_interrupter_from_epoll();

      // This constant is used for __n_submissions_in_flight to indicate that no new submissions
      // to this context will be completed by this context.
      static constexpr int no_new_submissions = -1;

      std::atomic<bool> is_running_;
      std::atomic<int> n_submissions_in_flight_{0};

      task_queue task_queue_;
      atomic_task_queue requests_;
      std::optional<stdexec::in_place_stop_source> stop_source_;
      bool processed_outstanding_submitted_;
      exec::safe_file_descriptor epoll_fd_;
      exec::safe_file_descriptor timer_fd_;
      // timer_heap timers_;
      // std::optional<time_point> current_earliest_due_time_;
      // bool timers_are_dirty_;
      // eventfd_interrupter interrupter_;
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

    inline int epoll_context::create_timer() {
      int fd = ::timerfd_create(CLOCK_MONOTONIC, 0);
      if (fd < 0) {
        throw std::system_error{static_cast<int>(errno), std::system_category(), "timer_fd create"};
      }
      return fd;
    }

    inline void epoll_context::add_timer_to_epoll() {
    }

    inline void epoll_context::remove_timer_from_epoll() {
      epoll_event event = {};
      if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, timer_fd_, &event) < 0) {
        throw std::system_error{
          static_cast<int>(errno), std::system_category(), "epoll_ctl_del timer_fd"};
      }
    }

    inline void epoll_context::add_interrupter_to_epoll() {
    }

    inline void epoll_context::remove_interrupter_from_epoll() {
    }

    // !!!Stores the address of the context owned by the current thread
    static thread_local epoll_context* current_thread_context;

    static constexpr uint32_t epoll_event_max_count = 256;

    inline void epoll_context::acquire_completion_queue_items() {
      epoll_event events[epoll_event_max_count];
      int wait_timeout = task_queue_.empty() ? -1 : 0;
      int result = ::epoll_wait(epoll_fd_, events, epoll_event_max_count, wait_timeout);
      if (result < 0) {
        throw std::system_error{
          static_cast<int>(errno), std::system_category(), "epoll_wait_return_error"};
      }

      // TODO(xiaoming) useless
      constexpr void* interrupter = nullptr;
      constexpr void* timer_data = nullptr;

      // temporary queue of newly completed items.
      task_queue tmp_task_queue;
      for (int i = 0; i < result; ++i) {
        if (events[i].data.ptr == interrupter) {
          // evenfd wakeup
        } else if (events[i].data.ptr == timer_data) {
          // timers op
        } else {
          // tasks
          // collect task
        }
      }
      schedule_local(std::move(tmp_task_queue));
    }

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

    inline bool epoll_context::try_schedule_remote_to_local() noexcept {
    }


  }; // namespace epoll

  using epoll_context = epoll::epoll_context;
} // namespace sio
