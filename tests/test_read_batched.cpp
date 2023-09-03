#include <sio/read_batched.hpp>
#include <sio/io_uring/file_handle.hpp>

#include <exec/when_any.hpp>

#include <catch2/catch.hpp>

TEST_CASE("read_batched - Read from a file", "[read_batched]") {
  exec::safe_file_descriptor fd{::memfd_create("test", 0)};
  REQUIRE(::ftruncate(fd, 4096) == 0);
  {
    const int value = 42;
    REQUIRE(::pwrite(fd, &value, sizeof(value), 0) == sizeof(value));
  }
  {
    const int value = 4242;
    REQUIRE(::pwrite(fd, &value, sizeof(value), 1024) == sizeof(value));
  }
  {
    const int value = 424242;
    REQUIRE(::pwrite(fd, &value, sizeof(value), 2048) == sizeof(value));
  }
  exec::io_uring_context context{};
  sio::io_uring::native_fd_handle fdh{context, std::move(fd)};
  sio::io_uring::seekable_byte_stream stream{std::move(fdh)};
  using offset_type = sio::async::offset_type_of_t<decltype(stream)>;
  offset_type offsets[3] = {0, 1024, 2048};
  int values[3] = {};
  std::span<std::byte> bytes[3] = {
    std::as_writable_bytes(std::span(&values[0], 1)),
    std::as_writable_bytes(std::span(&values[1], 1)),
    std::as_writable_bytes(std::span(&values[2], 1))};
  auto sndr = sio::async::read_batched(stream, bytes, offsets);
  stdexec::sync_wait(exec::when_any(std::move(sndr), context.run()));
  CHECK(values[0] == 42);
  CHECK(values[1] == 4242);
  CHECK(values[2] == 424242);
}