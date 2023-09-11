#include "./static_io_pool.hpp"

namespace sio {
  inline constexpr const int no_thread_context = -1;

  thread_local int this_thread_context = no_thread_context;

  static void thread_main(exec::io_uring_context& context, std::size_t this_thread_num) {
    this_thread_context = this_thread_num;
    context.run_until_stopped();
  }

  static_io_pool::static_io_pool(std::size_t nthreads) {
    contexts_.resize(nthreads);
    threads_.reserve(nthreads);
    for (std::size_t i = 0; i < nthreads; ++i) {
      threads_.emplace_back(thread_main, std::ref(contexts_[i]), i);
    }
  }

  void static_io_pool::submit(exec::__io_uring::__task* task) noexcept {
    std::size_t current_context = 0;
    if (this_thread_context == no_thread_context) {
      current_context = current_context_.fetch_add(1, std::memory_order_relaxed) % contexts_.size();
    } else {
      current_context = this_thread_context;
    }
    exec::io_uring_context& context = contexts_[current_context];
    context.submit(task);
    context.wakeup();
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