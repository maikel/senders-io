#include <sio/file_handle.hpp>

#include <exec/task.hpp>
#include <fmt/core.h>

template <class Tp>
using task = exec::basic_task<Tp, exec::__task::__default_task_context_impl<__scheduler_affinity::__none>>;

task<void> echo_input(exec::io_uring_context& context) {
  fmt::print("Hello, world!\n");
  co_return;
}

int main() {
  exec::io_uring_context context{};
  stdexec::start_detached(echo_input(context));
  context.run_until_empty();
}