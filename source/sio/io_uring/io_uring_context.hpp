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

#include "../concepts.hpp"
#include "../spmc_queue.hpp"

#include <exec/linux/io_uring_context.hpp>

namespace sio::io_uring {
  using task = exec::__io_uring::__task;
  using task_vtable = exec::__io_uring::__task_vtable;

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
  struct io_task_facade : task {
    static bool ready_(task* pointer) noexcept {
      io_task_facade* self = static_cast<io_task_facade*>(pointer);
      return self->base_.ready();
    }

    static void submit_(task* pointer, ::io_uring_sqe& sqe) noexcept {
      io_task_facade* self = static_cast<io_task_facade*>(pointer);
      self->base_.submit(sqe);
    }

    static void complete_(task* pointer, const ::io_uring_cqe& cqe) noexcept {
      io_task_facade* self = static_cast<io_task_facade*>(pointer);
      self->base_.complete(cqe);
    }

    static constexpr task_vtable vtable{&ready_, &submit_, &complete_};

    template <class... Args>
      requires stdexec::constructible_from<Base, std::in_place_t, task*, Args...>
    io_task_facade(std::in_place_t, Args&&... args) noexcept(
      nothrow_constructible_from<Base, task*, Args...>)
      : task{vtable}
      , base_(std::in_place, static_cast<task*>(this), static_cast<Args&&>(args)...) {
    }

    template <class... Args>
      requires stdexec::constructible_from<Base, Args...>
    io_task_facade(std::in_place_t, Args&&... args) noexcept(
      nothrow_constructible_from<Base, Args...>)
      : task{vtable}
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
    class type : public task {
      Base* op_;
     public:
      static bool ready_(task*) noexcept {
        return false;
      }

      static void submit_(task* pointer, ::io_uring_sqe& sqe) noexcept {
        type* self = static_cast<type*>(pointer);
        self->submit(sqe);
      }

      static void complete_(task* pointer, const ::io_uring_cqe& cqe) noexcept {
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

      static constexpr task_vtable vtable{&ready_, &submit_, &complete_};

      explicit type(Base* op) noexcept
        : task(vtable)
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
    task* parent_;
    Base base_;

    template <class... Args>
    impl_base(task* parent, std::in_place_t, Args&&... args) noexcept(
      nothrow_constructible_from<Base, Args...>)
      : parent_{parent}
      , base_((Args&&) args...) {
    }
  };

  template <class Base>
  struct impl_base<Base, true> {
    task* parent_;
    Base base_;

    template <class... Args>
    impl_base(task* parent, std::in_place_t, Args&&... args) noexcept(
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
      impl(std::in_place_t, task* parent, Args&&... args) noexcept(
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

  using context_base = exec::__io_uring::__context_base;

  template <class T>
  using atomic_ref = __atomic_ref<T>;

  using task_queue = exec::__io_uring::__task_queue;

  using atomic_task_queue = exec::__io_uring::__atomic_task_queue;

  struct submission_result {
    __u32 n_submitted;
    task_queue pending;
    task_queue ready;
  };

  // This class implements the io_uring submission queue.
  class submission_queue {
    atomic_ref<__u32> head_;
    atomic_ref<__u32> tail_;
    __u32* array_;
    ::io_uring_sqe* entries_;
    __u32 mask_;
    __u32 n_total_slots_;
   public:
    explicit submission_queue(
      const exec::memory_mapped_region& region,
      const exec::memory_mapped_region& sqes_region,
      const ::io_uring_params& params);

    submission_result
      submit(spmc_queue<task>& tasks, __u32 max_submissions, bool is_stopped) noexcept;

    submission_result submit(task_queue tasks, __u32 max_submissions, bool is_stopped) noexcept;
  };

  using completion_queue = exec::__io_uring::__completion_queue;

  class io_uring_context;

  struct wakeup_operation : task {
    io_uring_context* context_ = nullptr;
    int eventfd_ = -1;
    std::uint64_t buffer_ = 0;

    static bool ready_(task*) noexcept;

    static void submit_(task* pointer, ::io_uring_sqe& entry) noexcept;

    static void complete_(task* pointer, const ::io_uring_cqe& entry) noexcept;

    static constexpr task_vtable vtable{&ready_, &submit_, &complete_};

    wakeup_operation(io_uring_context* ctx, int eventfd) noexcept;

    void start() noexcept;
  };

  class io_uring_context : context_base {
   public:
    explicit io_uring_context(
      std::size_t spmc_queue_size = 1024,
      unsigned iodepth = 1024,
      unsigned flags = 0);

    void wakeup();

    void reset();

    void request_stop();

    bool stop_requested() const noexcept;

    stdexec::in_place_stop_token get_stop_token() const noexcept;

    bool is_running() const noexcept;

    void finish();

    /// \brief Submits the given task to the io_uring.
    bool submit(task* op) noexcept;

    /// \brief Submits the given task to the io_uring.
    bool submit_important(task* op) noexcept;

    /// \brief Steals a task from the pending io_urings tasks queue.
    task* steal() noexcept;

    /// @brief Submit any pending tasks and complete any ready tasks.
    ///
    /// This function is not thread-safe and must only be called from the thread that drives the io context.
    void run_some() noexcept;

    /// @brief Submit any pending tasks and complete any ready tasks.
    void run_until_stopped();

    void run_until_empty();

    struct on_stop {
      io_uring_context& context_;

      void operator()() const noexcept {
        context_.request_stop();
      }
    };

    template <class Rcvr>
    struct run_op {
      Rcvr __rcvr_;
      io_uring_context& __context_;
      exec::until __mode_;

      using on_stopped_callback = typename stdexec::stop_token_of_t<
        stdexec::env_of_t<Rcvr&>>::template callback_type<on_stop>;

      void start(stdexec::start_t) noexcept {
        std::optional<on_stopped_callback> __callback(
          std::in_place, stdexec::get_stop_token(stdexec::get_env(__rcvr_)), on_stop{__context_});
        try {
          if (__mode_ == exec::until::stopped) {
            __context_.run_until_stopped();
          } else {
            __context_.run_until_empty();
          }
        } catch (...) {
          __callback.reset();
          stdexec::set_error(std::move(__rcvr_), std::current_exception());
        }
        __callback.reset();
        if (__context_.stop_requested()) {
          stdexec::set_stopped(std::move(__rcvr_));
        } else {
          stdexec::set_value(std::move(__rcvr_));
        }
      }
    };

    class run_sender {
     public:
      using is_sender = void;
      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()>;

     private:
      friend class io_uring_context;
      io_uring_context* __context_;
      exec::until __mode_;

      explicit run_sender(io_uring_context* ctx, exec::until mode) noexcept
        : __context_{ctx}
        , __mode_{mode} {
      }

     public:
      template <stdexec::receiver_of<completion_signatures> Rcvr>
      run_op<Rcvr> connect(stdexec::connect_t, Rcvr __rcvr) const noexcept {
        return run_op<Rcvr>{static_cast<Rcvr&&>(__rcvr), *__context_, __mode_};
      }
    };

    run_sender run(exec::until __mode = exec::until::stopped) {
      return run_sender{this, __mode};
    }

   private:
    // This constant is used for __n_submissions_in_flight to indicate that no new submissions
    // to this context will be completed by this context.
    static constexpr int no_new_submissions = -1;

    std::atomic<bool> is_running_{false};
    std::atomic<int> n_submissions_in_flight_{0};
    std::atomic<bool> break_loop_{false};
    std::ptrdiff_t n_total_submitted_{0};
    std::ptrdiff_t n_newly_submitted_{0};
    std::optional<stdexec::in_place_stop_source> stop_source_{std::in_place};
    completion_queue completion_queue_;
    submission_queue submission_queue_;
    task_queue pending_{};
    task_queue high_priority_pending_{};
    atomic_task_queue requests_{};
    std::vector<std::atomic<task*>> stealable_tasks_buffer_;
    spmc_queue<task> stealable_tasks_;
    wakeup_operation wakeup_operation_;
    std::atomic<std::thread::id> active_thread_id_{};
  };

  template <class Receiver>
  struct ictx_schedule_operation {
    struct impl {
      io_uring_context& context_;
      Receiver receiver_;

      impl(io_uring_context& context, Receiver receiver)
        : context_{context}
        , receiver_{std::move(receiver)} {
      }

      io_uring_context& context() const noexcept {
        return context_;
      }

      static constexpr std::true_type ready() noexcept {
        return {};
      }

      static constexpr void submit(::io_uring_sqe&) noexcept {
      }

      void complete(const ::io_uring_cqe& cqe) noexcept {
        auto token = stdexec::get_stop_token(stdexec::get_env(receiver_));
        if (cqe.res == -ECANCELED || context_.stop_requested() || token.stop_requested()) {
          stdexec::set_stopped(std::move(receiver_));
        } else {
          stdexec::set_value(std::move(receiver_));
        }
      }
    };

    using type = io_task_facade<impl>;
  };
}