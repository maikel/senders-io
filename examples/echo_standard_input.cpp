#include <sio/file_handle.hpp>

#include <exec/task.hpp>

#include <iostream>

template <class Tp>
using task = exec::basic_task<
  Tp,
  exec::__task::__default_task_context_impl<exec::__task::__scheduler_affinity::__none>>;

template <std::size_t N>
std::span<const std::byte> as_bytes(const char (&array)[N]) {
  std::span<const char> span(array);
  return std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(span.data()), span.size_bytes());
}

task<void> hello(sio::io_uring::byte_stream out) {
  const char buffer[] = "Hello, world!\n";
  co_await sio::async::write(out, as_bytes(buffer));
  co_return;
}

int main() {
  exec::io_uring_context context{};
  sio::io_uring::native_fd_handle stdout_handle{context, STDOUT_FILENO};
  sio::io_uring::byte_stream out(stdout_handle);
  stdexec::sync_wait(stdexec::when_all(hello(out), context.run(exec::until::empty)));
}