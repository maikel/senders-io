#include "sio/assert.hpp"
#include "sio/epoll/epoll_context.hpp"

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>
#include <sys/socket.h>
#include <thread>
#include <type_traits>

using namespace sio;
using namespace sio::epoll;
using namespace std;

struct increment_operation : operation_base {
  ~increment_operation() {
    CHECK(n == 1);
  }

  static bool ready(operation_base*) noexcept {
    return false;
  }

  static void execute(operation_base* op) noexcept {
    auto self = static_cast<increment_operation*>(op);
    ++self->n;
  }

  static void complete(operation_base*, const std::error_code& ec) noexcept {
  }

  static constexpr operation_vtable vtable{&ready, &execute, &complete};

  increment_operation()
    : operation_base(vtable) {
  }

  int n = 0;
};

TEST_CASE("schedule operations to context from multi-threads", "[epoll_context]") {
  sio::epoll_context ctx{};
  constexpr int task_cnt = 50000;
  constexpr int thread_cnt = 20;
  atomic_int total = 0;
  std::vector<std::jthread> threads;

  for (int i = 0; i < thread_cnt; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < task_cnt; ++j) {
        auto op = new increment_operation; // delete op
        CHECK(ctx.schedule(op));
      }
      total.fetch_add(task_cnt);
    });
  }

  // Wait for schedule all operations to context.
  while (total != task_cnt * thread_cnt) {
  }

  ctx.op_queue_.append(ctx.requests_.pop_all());
  CHECK(ctx.execute_operations() == task_cnt * thread_cnt);
}

TEST_CASE("check value of members in a stopped context", "[epoll_context]") {
  epoll_context ctx{};
  std::thread io_thread([&] { ctx.run_until_stopped(); });
  ctx.request_stop();
  // wait until really stopped
  while (ctx.is_running()) {
  }
  io_thread.join();

  CHECK(ctx.stop_requested());
  CHECK(!ctx.break_loop_);
  CHECK(ctx.submissions_in_flight_ == epoll_context::no_new_submissions);
  CHECK(ctx.epoll_submitted_ == 0);
  CHECK(ctx.op_queue_.empty());
  CHECK(ctx.requests_.empty());
  CHECK(ctx.stop_source_.has_value());
}

TEST_CASE("check value of members in a stopped context with all tasks done", "[epoll_context]") {
  epoll_context ctx{};
  constexpr int task_cnt = 50000;
  for (int i = 0; i < task_cnt; ++i) {
    increment_operation* op = new increment_operation; // delete op
    CHECK(ctx.schedule(op));
  }
  ctx.run_until_empty();
  CHECK(!ctx.is_running());
  CHECK(!ctx.stop_requested());
  CHECK(!ctx.break_loop_);
  CHECK(ctx.submissions_in_flight_ == 0);
  CHECK(ctx.epoll_submitted_ == 1);
  CHECK(ctx.op_queue_.empty());
  CHECK(ctx.requests_.empty());
  CHECK(ctx.stop_source_.has_value());
}

TEST_CASE("Context wakeup multi-times by multi-threads should be safe", "[epoll_context]") {
  epoll_context ctx{};
  std::jthread io_thread([&] { ctx.run_until_stopped(); });

  // !wakeup when context is running
  constexpr size_t thread_cnt = 10;
  for (int n = 0; n < thread_cnt; ++n) {
    std::jthread thr([&ctx] {
      for (int i = 0; i < 1000; ++i) {
        ctx.wakeup();
      }
    });
  }

  ctx.request_stop();
  // !wakeup when context is abort to stop and stopped.
  for (int n = 0; n < thread_cnt; ++n) {
    std::jthread thr([&ctx] {
      for (int i = 0; i < 1000; ++i) {
        ctx.wakeup();
      }
    });
  }
}
