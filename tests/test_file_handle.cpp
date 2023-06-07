#include "sio/file_handle.hpp"

#include <exec/task.hpp>

#include <catch2/catch.hpp>

exec::task<void> print_file(sio::io_uring::byte_stream file) {
  char buffer[4096];
  sio::io_uring::byte_stream out({{file.context_, STDOUT_FILENO}});
  while (true) {
    ssize_t bytes_read = co_await file.read(buffer);
    if (bytes_read == 0) {
      co_return;
    }

    ssize_t bytes_written = co_await out.write(buffer);
    if (bytes_written != bytes_read) {
      co_return;
    }
  }
}

TEST_CASE("Open a path") {
  exec::io_uring_context context{};
  sio::io_uring::io_scheduler scheduler{&context};
  sio::io_uring::file file = sio::async::file(scheduler, "/dev/null");
  sio::io_uring::byte_stream out({context, STDOUT_FILENO});
}