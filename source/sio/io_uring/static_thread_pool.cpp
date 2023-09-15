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
#include "./static_thread_pool.hpp"

namespace sio::io_uring {
  inline constexpr const int no_thread_context = -1;

  thread_local int this_thread_context = no_thread_context;

  static void thread_main(sio::io_uring::io_uring_context& context, std::size_t this_thread_num) {
    this_thread_context = this_thread_num;
    context.run_until_stopped();
  }

  scheduler tag_invoke(
    stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
    const pool_env& env) noexcept {
    return scheduler{env.context_};
  }

  static_thread_pool::static_thread_pool(std::size_t nthreads, unsigned iodepth) {
    threads_.reserve(nthreads);
    for (std::size_t i = 0; i < nthreads; ++i) {
      contexts_.emplace_back(1024, iodepth, 0);
      threads_.emplace_back(thread_main, std::ref(contexts_[i]), i);
    }
  }

  bool static_thread_pool::submit(task* task) noexcept {
    std::size_t current_context = 0;
    if (this_thread_context == no_thread_context) {
      current_context = current_context_.fetch_add(1, std::memory_order_relaxed) % contexts_.size();
    } else {
      current_context = this_thread_context;
    }
    sio::io_uring::io_uring_context& context = contexts_[current_context];
    auto rc = context.submit(task);
    context.wakeup();
    return rc;
  }

  void static_thread_pool::wakeup() noexcept {
  }

  bool static_thread_pool::stop_requested() const noexcept {
    return stop_source_.stop_requested();
  }

  void static_thread_pool::request_stop() noexcept {
    stop_source_.request_stop();
  }

  stdexec::in_place_stop_token static_thread_pool::get_stop_token() const noexcept {
    return stop_source_.get_token();
  }

  std::span<std::thread> static_thread_pool::threads() noexcept {
    return std::span<std::thread>(threads_);
  }

  void static_thread_pool::stop() noexcept {
    for (auto& context: contexts_) {
      context.request_stop();
    }
    for (auto& thread: threads_) {
      thread.join();
    }
  }

  scheduler static_thread_pool::get_scheduler() noexcept {
    return scheduler{this};
  }

  static_thread_pool::~static_thread_pool() {
    stop();
  }
}