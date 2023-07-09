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

TEST_CASE("epoll_context schedule operations in multiply threads", "[epoll_context]") {
  sio::epoll_context ctx{};
  constexpr int task_cnt = 50000;
  constexpr int thread_cnt = 20;
  std::vector<std::jthread> threads;

  for (int i = 0; i < thread_cnt; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < task_cnt; ++j) {
        increment_operation* op = new increment_operation;
        CHECK(ctx.schedule(op));
      }
    });
  }

  std::this_thread::sleep_for(600ms);
  ctx.op_queue_.append(ctx.requests_.pop_all());
  CHECK(ctx.execute_operations() == task_cnt * thread_cnt);
}

TEST_CASE("epoll context should wakeup from ::epoll_wait", "[epoll_context]") {
  epoll_context ctx{};
  std::jthread io_thread([&] { CHECK(ctx.acquire_operations_from_epoll() == 0); });
  ctx.wakeup();
}