#include <sio/io_uring/file_handle.hpp>

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

template <std::size_t N>
std::span<std::byte> as_bytes(char (&array)[N]) {
  std::span<char> span(array);
  return std::span<std::byte>(
    reinterpret_cast<std::byte*>(span.data()), span.size_bytes());
}


task<void> write_all(sio::async::writable_byte_stream auto out, std::span<const std::byte> buffer) {
  while (!buffer.empty()) {
    std::size_t nbytes = co_await sio::async::write(out, buffer);
    buffer = buffer.subspan(nbytes);
  }
}

task<void> echo(sio::async::readable_byte_stream auto in, sio::async::writable_byte_stream auto out) {
  char buffer[64] = {};
  std::size_t nbytes = co_await sio::async::read(in, as_bytes(buffer));
  while (nbytes) {
    co_await write_all(out, as_bytes(buffer).subspan(0, nbytes));
    nbytes = co_await sio::async::read(in, as_bytes(buffer));
  }
  co_return;
}

int main() {
  exec::io_uring_context context{};
  sio::io_uring::native_fd_handle stdout_handle{context, STDOUT_FILENO};
  sio::io_uring::byte_stream out(stdout_handle);
  sio::io_uring::native_fd_handle stdin_handle{context, STDIN_FILENO};
  sio::io_uring::byte_stream in(stdin_handle);
  static_assert(sio::async::byte_stream<sio::io_uring::byte_stream>);
  stdexec::sync_wait(stdexec::when_all(echo(in, out), context.run(exec::until::empty)));
}