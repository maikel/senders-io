#include <sio/io_uring/static_io_pool.hpp>

#include <sio/io_uring/file_handle.hpp>

#include <catch2/catch.hpp>

TEST_CASE("static_io_pool - Construct and destruct", "[static_io_pool]") {
  sio::io_uring::static_io_pool pool(1);
  CHECK(pool.threads().size() == 1);
}

TEST_CASE("static_io_pool - file handle", "[static_io_pool]") {
  sio::io_uring::static_io_pool pool(2);
  exec::safe_file_descriptor fd(::memfd_create("test", MFD_CLOEXEC));
  REQUIRE(fd > 0);
  REQUIRE(::ftruncate(fd, 4096) == 0);
  sio::io_uring::basic_native_fd_handle<sio::io_uring::static_io_pool> fdh{pool, fd};
  sio::io_uring::basic_byte_stream<sio::io_uring::static_io_pool> stream{fdh};
  CHECK(stream.get() > 0);
  auto write = sio::async::write_some(stream, std::as_bytes(std::span("foo")));
  stdexec::sync_wait(write);
  exec::memory_mapped_region region = exec::__io_uring::__map_region(fd, 0, 4096);
  REQUIRE(region);
  CHECK(std::memcmp(region.data(), "foo", 3) == 0);
}