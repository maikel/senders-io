#include "sio/file_handle.hpp"

#include <exec/task.hpp>

#include <catch2/catch.hpp>

template <class Tp>
using task = exec::basic_task<
  Tp,
  exec::__task::__default_task_context_impl<exec::__task::__scheduler_affinity::__none>>;

// exec::task<void> print_file(sio::io_uring::byte_stream file) {
// char buffer[4096];
// while (true) {
//   ssize_t bytes_read = co_await file.read(sio::async::read, buffer);
//   if (bytes_read == 0) {
//     co_return;
//   }

//   ssize_t bytes_written = co_await out.write(sio::async::write, buffer);
//   if (bytes_written != bytes_read) {
//     co_return;
//   }
// }
// }

task<void> no_op(sio::io_uring::path_handle) {
  co_return;
}

template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  stdexec::sync_wait(stdexec::when_all(std::forward<Sender>(sender), context.run(exec::until::empty)));
}

TEST_CASE("Open a path") {
  exec::io_uring_context context{};
  sio::io_uring::io_scheduler scheduler{&context};
  sio::io_uring::path path = sio::async::path(scheduler, "/dev/null");
  sync_wait(context, sio::async::use_resources(no_op, path));
}