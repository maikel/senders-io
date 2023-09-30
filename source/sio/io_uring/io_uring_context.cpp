#include "./io_uring_context.hpp"

namespace sio::io_uring {
  static void throw_error_code_if(bool cond, int ec) {
    if (cond) {
      throw std::system_error(ec, std::system_category());
    }
  }

  static void stop(task* op) noexcept {
    ::io_uring_cqe cqe{};
    cqe.res = -ECANCELED;
    cqe.user_data = std::bit_cast<__u64>(op);
    op->__vtable_->__complete_(op, cqe);
  }

  bool wakeup_operation::ready_(task*) noexcept {
    return false;
  }

  void wakeup_operation::submit_(task* pointer, ::io_uring_sqe& sqe) noexcept {
    wakeup_operation* op = static_cast<wakeup_operation*>(pointer);
    sqe = ::io_uring_sqe{};
    sqe.fd = op->eventfd_;
    sqe.opcode = IORING_OP_READ;
    sqe.addr = std::bit_cast<__u64>(&op->buffer_);
    sqe.len = sizeof(op->buffer_);
  }

  void wakeup_operation::complete_(task* pointer, const ::io_uring_cqe& cqe) noexcept {
    wakeup_operation* op = static_cast<wakeup_operation*>(pointer);
    op->start();
  }

  void wakeup_operation::start() noexcept {
    if (!context_->stop_requested()) {
      context_->submit_important(this);
    }
  }

  wakeup_operation::wakeup_operation(io_uring_context* context, int eventfd) noexcept
    : task{vtable}
    , context_(context)
    , eventfd_{eventfd} {
  }

  submission_queue::submission_queue(
    const exec::memory_mapped_region& region,
    const exec::memory_mapped_region& sqes_region,
    const ::io_uring_params& params)
    : head_{*exec::__io_uring::__at_offset_as<__u32*>(region.data(), params.sq_off.head)}
    , tail_{*exec::__io_uring::__at_offset_as<__u32*>(region.data(), params.sq_off.tail)}
    , array_{exec::__io_uring::__at_offset_as<__u32*>(region.data(), params.sq_off.array)}
    , entries_{static_cast<::io_uring_sqe*>(sqes_region.data())}
    , mask_{*exec::__io_uring::__at_offset_as<__u32*>(region.data(), params.sq_off.ring_mask)}
    , n_total_slots_{params.sq_entries} {
  }

  submission_result
    submission_queue::submit(task_queue tasks, __u32 max_submissions, bool is_stopped) noexcept {
    __u32 tail = tail_.load(std::memory_order_relaxed);
    __u32 head = head_.load(std::memory_order_acquire);
    __u32 current_count = tail - head;
    STDEXEC_ASSERT(current_count <= n_total_slots_);
    max_submissions = std::min(max_submissions, n_total_slots_ - current_count);
    submission_result result{};
    task* op = nullptr;
    while (!tasks.empty() && result.n_submitted < max_submissions) {
      const __u32 index = tail & mask_;
      ::io_uring_sqe& sqe = entries_[index];
      op = tasks.pop_front();
      STDEXEC_ASSERT(op->__vtable_);
      if (op->__vtable_->__ready_(op)) {
        result.ready.push_back(op);
      } else {
        op->__vtable_->__submit_(op, sqe);
        if (is_stopped && sqe.opcode != IORING_OP_ASYNC_CANCEL) {
          stop(op);
        } else {
          sqe.user_data = std::bit_cast<__u64>(op);
          array_[index] = index;
          ++result.n_submitted;
          ++tail;
        }
      }
    }
    tail_.store(tail, std::memory_order_release);
    while (!tasks.empty()) {
      op = tasks.pop_front();
      if (op->__vtable_->__ready_(op)) {
        result.ready.push_back(op);
      } else {
        result.pending.push_back(op);
      }
    }
    return result;
  }

  submission_result submission_queue::submit(
    spmc_queue<task>& tasks,
    __u32 max_submissions,
    bool is_stopped) noexcept {
    __u32 tail = tail_.load(std::memory_order_relaxed);
    __u32 head = head_.load(std::memory_order_acquire);
    __u32 current_count = tail - head;
    STDEXEC_ASSERT(current_count <= n_total_slots_);
    max_submissions = std::min(max_submissions, n_total_slots_ - current_count);
    submission_result result{};
    task* op = tasks.pop_front();
    while (op && result.n_submitted < max_submissions) {
      const __u32 index = tail & mask_;
      ::io_uring_sqe& sqe = entries_[index];
      STDEXEC_ASSERT(op->__vtable_);
      if (op->__vtable_->__ready_(op)) {
        result.ready.push_back(op);
      } else {
        op->__vtable_->__submit_(op, sqe);
        if (is_stopped && sqe.opcode != IORING_OP_ASYNC_CANCEL) {
          stop(op);
        } else {
          sqe.user_data = std::bit_cast<__u64>(op);
          array_[index] = index;
          ++result.n_submitted;
          ++tail;
        }
      }
      op = tasks.pop_front();
    }
    tail_.store(tail, std::memory_order_release);
    if (op) {
      tasks.push_back(op);
    }
    return result;
  }

  io_uring_context::io_uring_context(std::size_t spmc_queue_size, unsigned iodepth, unsigned flags)
    : context_base{std::max(iodepth, 2u), flags}
    , completion_queue_{__completion_queue_region_ ? __completion_queue_region_ : __submission_queue_region_, __params_}
    , submission_queue_{__submission_queue_region_, __submission_queue_entries_, __params_}
    , stealable_tasks_buffer_(spmc_queue_size)
    , stealable_tasks_{stealable_tasks_buffer_}
    , wakeup_operation_{this, __eventfd_} {
  }

  void io_uring_context::wakeup() {
    std::uint64_t __wakeup = 1;
    if (
      stop_requested()
      || active_thread_id_.load(std::memory_order_relaxed) != std::this_thread::get_id()) {
      throw_error_code_if(::write(__eventfd_, &__wakeup, sizeof(__wakeup)) == -1, errno);
    }
  }

  /// @brief Resets the io context to its initial state.
  void io_uring_context::reset() {
    if (is_running_.load(std::memory_order_relaxed) || n_total_submitted_ > 0) {
      throw std::runtime_error("sio::io_uring_context::reset() called on a running context");
    }
    n_submissions_in_flight_.store(0, std::memory_order_relaxed);
    stop_source_.reset();
    stop_source_.emplace();
  }

  void io_uring_context::request_stop() {
    stop_source_->request_stop();
    wakeup();
  }

  bool io_uring_context::stop_requested() const noexcept {
    return stop_source_->stop_requested();
  }

  stdexec::in_place_stop_token io_uring_context::get_stop_token() const noexcept {
    return stop_source_->get_token();
  }

  bool io_uring_context::is_running() const noexcept {
    return is_running_.load(std::memory_order_relaxed);
  }

  /// @brief  Breaks out of the run loop of the io context without stopping the context.
  void io_uring_context::finish() {
    break_loop_.store(true, std::memory_order_release);
    wakeup();
  }

  /// \brief Submits the given task to the io_uring.
  /// \returns true if the task was submitted, false if this io context and this task is have been stopped.
  bool io_uring_context::submit(task* op) noexcept {
    // As long as the number of in-flight submissions is not no_new_submissions, we can
    // increment the counter and push the operation onto the queue.
    // If the number of in-flight submissions is no_new_submissions, we have already
    // finished the stop operation of the io context and we can immediately stop the operation inline.
    // Remark: As long as the stopping is in progress we can still submit new operations.
    // But no operation will be submitted to io uring unless it is a cancellation operation.
    int n = 0;
    while (n != no_new_submissions
           && !n_submissions_in_flight_.compare_exchange_weak(
             n, n + 1, std::memory_order_acquire, std::memory_order_relaxed))
      ;
    if (n == no_new_submissions) {
      stop(op);
      return false;
    } else {
      requests_.push_front(op);
      [[maybe_unused]] int prev = n_submissions_in_flight_.fetch_sub(1, std::memory_order_relaxed);
      STDEXEC_ASSERT(prev > 0);
      return true;
    }
  }

  bool io_uring_context::submit_important(task* op) noexcept {
    // As long as the number of in-flight submissions is not no_new_submissions, we can
    // increment the counter and push the operation onto the queue.
    // If the number of in-flight submissions is no_new_submissions, we have already
    // finished the stop operation of the io context and we can immediately stop the operation inline.
    // Remark: As long as the stopping is in progress we can still submit new operations.
    // But no operation will be submitted to io uring unless it is a cancellation operation.
    int n = 0;
    while (n != no_new_submissions
           && !n_submissions_in_flight_.compare_exchange_weak(
             n, n + 1, std::memory_order_acquire, std::memory_order_relaxed))
      ;
    if (n == no_new_submissions) {
      stop(op);
      return false;
    } else {
      high_priority_pending_.push_front(op);
      [[maybe_unused]] int prev = n_submissions_in_flight_.fetch_sub(1, std::memory_order_relaxed);
      STDEXEC_ASSERT(prev > 0);
      return true;
    }
  }

  static void add_to_spmc_queue(spmc_queue<task>& stealable_queue, task_queue& pending) {
    while (!pending.empty()) {
      task* op = pending.pop_front();
      if (!stealable_queue.push_back(op)) {
        pending.push_front(op);
        break;
      }
    }
  }

  static task_queue fetch_from_spmc_queue(
    spmc_queue<task>& stealable_queue,
    task_queue& pending,
    std::size_t max_submissions) noexcept {
    task_queue result;
    std::size_t counter = 0;
    while (counter < max_submissions) {
      task* op = stealable_queue.pop_front();
      if (!op) {
        break;
      }
      result.push_back(op);
      ++counter;
    }
    return result;
  }

  /// @brief Submit any pending tasks and complete any ready tasks.
  ///
  /// This function is not thread-safe and must only be called from the thread that drives the io context.
  void io_uring_context::run_some() noexcept {
    n_total_submitted_ -= completion_queue_.complete();
    STDEXEC_ASSERT(
      0 <= n_total_submitted_
      && n_total_submitted_ <= static_cast<std::ptrdiff_t>(__params_.cq_entries));
    __u32 max_submissions = __params_.cq_entries - static_cast<__u32>(n_total_submitted_);
    auto result = submission_queue_.submit(
      std::move(high_priority_pending_), max_submissions, stop_source_->stop_requested());
    n_total_submitted_ += result.n_submitted;
    n_newly_submitted_ += result.n_submitted;
    high_priority_pending_.append(std::move(result.pending));
    STDEXEC_ASSERT(n_total_submitted_ <= static_cast<std::ptrdiff_t>(__params_.cq_entries));
    n_total_submitted_ -= completion_queue_.complete((task_queue&&) result.ready);
    STDEXEC_ASSERT(0 <= n_total_submitted_);
    pending_.append(requests_.pop_all());
    add_to_spmc_queue(stealable_tasks_, pending_);
    max_submissions = __params_.cq_entries - static_cast<__u32>(n_total_submitted_);
    result = submission_queue_.submit(
      stealable_tasks_, max_submissions, stop_source_->stop_requested());
    n_total_submitted_ += result.n_submitted;
    n_newly_submitted_ += result.n_submitted;
    STDEXEC_ASSERT(n_total_submitted_ <= static_cast<std::ptrdiff_t>(__params_.cq_entries));
    while (!result.ready.empty()) {
      n_total_submitted_ -= completion_queue_.complete((task_queue&&) result.ready);
      STDEXEC_ASSERT(0 <= n_total_submitted_);
      max_submissions = __params_.cq_entries - static_cast<__u32>(n_total_submitted_);
      result = submission_queue_.submit(
        std::move(high_priority_pending_), max_submissions, stop_source_->stop_requested());
      n_total_submitted_ += result.n_submitted;
      n_newly_submitted_ += result.n_submitted;
      high_priority_pending_.append(std::move(result.pending));
      STDEXEC_ASSERT(n_total_submitted_ <= static_cast<std::ptrdiff_t>(__params_.cq_entries));
      n_total_submitted_ -= completion_queue_.complete((task_queue&&) result.ready);
      STDEXEC_ASSERT(0 <= n_total_submitted_);
      pending_.append(requests_.pop_all());
      add_to_spmc_queue(stealable_tasks_, pending_);
      max_submissions = __params_.cq_entries - static_cast<__u32>(n_total_submitted_);
      result = submission_queue_.submit(
        stealable_tasks_, max_submissions, stop_source_->stop_requested());
      n_total_submitted_ += result.n_submitted;
      n_newly_submitted_ += result.n_submitted;
      STDEXEC_ASSERT(n_total_submitted_ <= static_cast<std::ptrdiff_t>(__params_.cq_entries));
    }
  }

  void io_uring_context::run_until_stopped() {
    bool expected_running = false;
    // Only one thread of execution is allowed to drive the io context.
    if (!is_running_.compare_exchange_strong(expected_running, true, std::memory_order_relaxed)) {
      throw std::runtime_error("exec::io_uring_context::run() called on a running context");
    } else {
      // Check whether we restart the context after a context-wide stop.
      // We have to reset the stop source in this case.
      int in_flight = n_submissions_in_flight_.load(std::memory_order_relaxed);
      if (in_flight == no_new_submissions) {
        stop_source_.emplace();
        // Make emplacement of stop source visible to other threads and open the door for new submissions.
        n_submissions_in_flight_.store(0, std::memory_order_release);
      } else {
        // This can only happen for the very first pass of run_until_stopped()
        wakeup_operation_.start();
      }
      active_thread_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }
    exec::scope_guard not_running{[&]() noexcept {
      is_running_.store(false, std::memory_order_relaxed);
      active_thread_id_.store(std::thread::id{}, std::memory_order_relaxed);
    }};
    pending_.append(requests_.pop_all());
    while (n_total_submitted_ > 0 || !pending_.empty() || !high_priority_pending_.empty()
           || !stealable_tasks_.empty()) {
      run_some();
      if (
        n_total_submitted_ == 0
        || (n_total_submitted_ == 1 && break_loop_.load(std::memory_order_acquire))) {
        break_loop_.store(false, std::memory_order_relaxed);
        break;
      }
      constexpr int __min_complete = 1;
      STDEXEC_ASSERT(
        0 <= n_total_submitted_
        && n_total_submitted_ <= static_cast<std::ptrdiff_t>(__params_.cq_entries));

      int rc = exec::__io_uring::__io_uring_enter(
        __ring_fd_, n_newly_submitted_, __min_complete, IORING_ENTER_GETEVENTS);
      throw_error_code_if(rc < 0 && rc != -EINTR, -rc);
      if (rc != -EINTR) {
        STDEXEC_ASSERT(rc <= n_newly_submitted_);
        n_newly_submitted_ -= rc;
      }
      n_total_submitted_ -= completion_queue_.complete();
      STDEXEC_ASSERT(0 <= n_total_submitted_);
      pending_.append(requests_.pop_all());
    }
    STDEXEC_ASSERT(n_total_submitted_ <= 1);
    if (stop_source_->stop_requested() && pending_.empty()) {
      STDEXEC_ASSERT(n_total_submitted_ == 0);
      // try to shutdown the request queue
      int n_in_flight_expected = 0;
      while (!n_submissions_in_flight_.compare_exchange_weak(
        n_in_flight_expected, no_new_submissions, std::memory_order_relaxed)) {
        if (n_in_flight_expected == no_new_submissions) {
          break;
        }
        n_in_flight_expected = 0;
      }
      STDEXEC_ASSERT(
        n_submissions_in_flight_.load(std::memory_order_relaxed) == no_new_submissions);
      // There could have been requests in flight. Complete all of them
      // and then stop it, finally.
      pending_.append(requests_.pop_all());
      submission_result result = submission_queue_.submit(
        (task_queue&&) pending_, __params_.cq_entries, true);
      STDEXEC_ASSERT(result.n_submitted == 0);
      STDEXEC_ASSERT(result.pending.empty());
      completion_queue_.complete((task_queue&&) result.ready);
    }
  }

  void io_uring_context::run_until_empty() {
    break_loop_.store(true, std::memory_order_relaxed);
    run_until_stopped();
  }
}