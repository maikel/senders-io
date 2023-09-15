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

#include "./io_uring_context.hpp"

#include <deque>
#include <span>

namespace sio::io_uring {

  class static_thread_pool;

  struct schedule_sender;

  struct scheduler {
    static_thread_pool* context_;

    schedule_sender schedule(stdexec::schedule_t) const noexcept;

    friend bool operator==(const scheduler& a, const scheduler& b) noexcept = default;
  };

  class static_thread_pool {
   public:
    explicit static_thread_pool(std::size_t nthreads, unsigned iodepth = 1024);
    ~static_thread_pool();

    scheduler get_scheduler() noexcept;

    bool submit(task* pointer) noexcept;

    stdexec::in_place_stop_token get_stop_token() const noexcept;

    void wakeup() noexcept;

    bool stop_requested() const noexcept;

    void request_stop() noexcept;

    std::span<std::thread> threads() noexcept;

    void stop() noexcept;

   private:
    stdexec::in_place_stop_source stop_source_;
    std::atomic<std::size_t> current_context_ = 0;
    std::deque<sio::io_uring::io_uring_context> contexts_;
    std::vector<std::thread> threads_;
  };

  template <class Receiver>
  struct schedule_operation : task {
    static_thread_pool* context_;
    Receiver receiver_;

    static bool ready_(task*) noexcept {
      return true;
    }

    static void submit_(task*, ::io_uring_sqe&) noexcept {
    }

    static void complete_(task* task, const ::io_uring_cqe& cqe) noexcept {
      auto* self = static_cast<schedule_operation*>(task);
      if (cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(self->receiver_));
      } else {
        stdexec::set_value(std::move(self->receiver_));
      }
    }

    static constexpr exec::__io_uring::__task_vtable vtable_{&ready_, &submit_, &complete_};

    explicit schedule_operation(static_thread_pool& pool, Receiver receiver) noexcept
      : task(vtable_)
      , context_(&pool)
      , receiver_(std::move(receiver)) {
    }

    void start(stdexec::start_t) noexcept {
      context_->submit(this);
    }
  };

  struct pool_env {
    static_thread_pool* context_;

    friend scheduler tag_invoke(
      stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
      const pool_env& env) noexcept;
  };

  struct schedule_sender {
    using is_sender = void;

    using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;

    static_thread_pool* context_;

    pool_env get_env(stdexec::get_env_t) const noexcept {
      return {context_};
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    schedule_operation<Receiver> connect(stdexec::connect_t, Receiver receiver) const noexcept {
      return schedule_operation<Receiver>{*context_, std::move(receiver)};
    }
  };

  inline schedule_sender scheduler::schedule(stdexec::schedule_t) const noexcept {
    return schedule_sender{context_};
  }
}