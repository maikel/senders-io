#pragma once

#include <exec/linux/io_uring_context.hpp>

#include <deque>
#include <span>

namespace sio::io_uring {

  class static_io_pool;

  struct schedule_sender;

  struct scheduler {
    static_io_pool* context_;

    schedule_sender schedule(stdexec::schedule_t) const noexcept;

    friend bool operator==(const scheduler& a, const scheduler& b) noexcept = default;
  };

  class static_io_pool {
   public:
    explicit static_io_pool(std::size_t nthreads);
    ~static_io_pool();

    scheduler get_scheduler() noexcept;

    bool submit(exec::__io_uring::__task* task) noexcept;

    stdexec::in_place_stop_token get_stop_token() const noexcept;

    void wakeup() noexcept;

    bool stop_requested() const noexcept;

    void request_stop() noexcept;

    std::span<std::thread> threads() noexcept;

    void stop() noexcept;

   private:
    stdexec::in_place_stop_source stop_source_;
    std::atomic<std::size_t> current_context_ = 0;
    std::deque<exec::io_uring_context> contexts_;
    std::vector<std::thread> threads_;
  };

  template <class Receiver>
  struct schedule_operation : exec::__io_uring::__task {
    static_io_pool* context_;
    Receiver receiver_;

    static bool ready_(__task*) noexcept {
      return true;
    }

    static void submit_(__task*, ::io_uring_sqe&) noexcept {
    }

    static void complete_(__task* task, const ::io_uring_cqe& cqe) noexcept {
      auto* self = static_cast<schedule_operation*>(task);
      if (cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(self->receiver_));
      } else {
        stdexec::set_value(std::move(self->receiver_));
      }
    }

    static constexpr exec::__io_uring::__task_vtable vtable_{&ready_, &submit_, &complete_};

    explicit schedule_operation(static_io_pool& pool, Receiver receiver) noexcept
      : __task(vtable_)
      , context_(&pool)
      , receiver_(std::move(receiver)) {
    }

    void start(stdexec::start_t) noexcept {
      context_->submit(this);
    }
  };

  struct schedule_sender {
    using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;

    static_io_pool* context_;

    friend scheduler tag_invoke(
      stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
      const schedule_sender& sender) noexcept {
      return scheduler{sender.context_};
    }

    schedule_sender get_env(stdexec::get_env_t) const noexcept {
      return *this;
    }

    template <stdexec::receiver_of<completion_signatures> Receiver>
    schedule_operation<Receiver> connect(Receiver receiver) const noexcept {
      return schedule_operation<Receiver>{*context_, std::move(receiver)};
    }
  };

  inline schedule_sender scheduler::schedule(stdexec::schedule_t) const noexcept {
    return schedule_sender{context_};
  }
}