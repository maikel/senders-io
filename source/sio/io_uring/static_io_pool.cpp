#include "./static_io_pool.hpp"

namespace sio::io_uring {
  inline constexpr const int no_thread_context = -1;

  thread_local int this_thread_context = no_thread_context;

  static void thread_main(exec::io_uring_context& context, std::size_t this_thread_num) {
    this_thread_context = this_thread_num;
    context.run_until_stopped();
  }

  static_io_pool::static_io_pool(std::size_t nthreads, unsigned iodepth) {
    threads_.reserve(nthreads);
    for (std::size_t i = 0; i < nthreads; ++i) {
      contexts_.emplace_back(iodepth);
      threads_.emplace_back(thread_main, std::ref(contexts_[i]), i);
    }
  }

  bool static_io_pool::submit(exec::__io_uring::__task* task) noexcept {
    std::size_t current_context = 0;
    if (this_thread_context == no_thread_context) {
      current_context = current_context_.fetch_add(1, std::memory_order_relaxed) % contexts_.size();
    } else {
      current_context = this_thread_context;
    }
    exec::io_uring_context& context = contexts_[current_context];
    auto rc = context.submit(task);
    context.wakeup();
    return rc;
  }

  void static_io_pool::wakeup() noexcept {
  }

  bool static_io_pool::stop_requested() const noexcept {
    return stop_source_.stop_requested();
  }

  void static_io_pool::request_stop() noexcept {
    stop_source_.request_stop();
  }

  stdexec::in_place_stop_token static_io_pool::get_stop_token() const noexcept {
    return stop_source_.get_token();
  }

  std::span<std::thread> static_io_pool::threads() noexcept {
    return std::span<std::thread>(threads_);
  }

  void static_io_pool::stop() noexcept {
    for (auto& context: contexts_) {
      context.request_stop();
    }
    for (auto& thread: threads_) {
      thread.join();
    }
  }

  static_io_pool::~static_io_pool() {
    stop();
  }
}