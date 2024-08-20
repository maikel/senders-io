/*
 * Copyright (c) 2024 Emmett Zhang
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sio/buffer.hpp"
#include "sio/io_uring/file_handle.hpp"
#include "sio/mutable_buffer.hpp"
#include "sio/sequence/buffered_sequence.hpp"
#include "sio/sequence/ignore_all.hpp"

#include <catch2/catch_all.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <exec/linux/io_uring_context.hpp>
#include <exec/when_any.hpp>

int create_memfd(std::string_view path) {
  int fd = memfd_create(path.data(), 0);
  REQUIRE(fd != -1);
  return fd;
}

std::string read_file(int fd) {
  constexpr std::size_t max_size = 1024;
  char buffer[max_size];

  std::size_t total = 0;
  while (true) {
    ::ssize_t nbytes = ::read(fd, buffer, max_size);
    if (nbytes == 0) {
      break;
    }
    REQUIRE(nbytes > -1);
    total += nbytes;
  }
  return {buffer, total};
}

void write_to_file(int fd, std::string_view content) {
  int ret = ::ftruncate(fd, content.size());
  REQUIRE(ret != -1);

  ret = ::write(fd, content.data(), content.size());
  REQUIRE(ret == content.size());
}

template <stdexec::sender Sender>
void sync_wait(exec::io_uring_context& context, Sender&& sender) {
  stdexec::sync_wait(exec::when_any(std::forward<Sender>(sender), context.run()));
}

TEST_CASE("buffered_sequence - with read_factory and single buffer", "[sio][buffered_sequence]") {
  auto ctx = exec::io_uring_context{};
  auto fd = create_memfd("with_read_factory");

  constexpr auto content = std::string_view{"hello world"};
  write_to_file(fd, content);
  auto factory = sio::io_uring::read_factory{&ctx, fd};

  // write to storage
  auto storage = std::string(content.size(), '0');
  auto buffer = sio::buffer(storage);
  auto buffered_read_some = sio::buffered_sequence(factory, buffer, 0);
  ::sync_wait(ctx, sio::ignore_all(std::move(buffered_read_some)));

  CHECK(storage == content);
}

TEST_CASE(
  "buffered_sequence - with read_factory and multiple buffers",
  "[sio][buffered_sequence]") {
  auto ctx = exec::io_uring_context{};
  auto fd = create_memfd("with_read_factory");

  constexpr auto content = std::string_view{"hello world"};
  write_to_file(fd, content);
  auto factory = sio::io_uring::read_factory{&ctx, fd};

  // write to storage
  auto storage1 = std::string(6, '0');
  auto storage2 = std::string(5, '0');
  auto array = std::array<sio::mutable_buffer, 2>{sio::buffer(storage1), sio::buffer(storage2)};
  auto buffers = std::span< sio::mutable_buffer>{array};
  auto buffered_read_some = sio::buffered_sequence(factory, buffers, 0);
  ::sync_wait(ctx, sio::ignore_all(std::move(buffered_read_some)));

  CHECK(storage1 == "hello ");
  CHECK(storage2 == "world");
}

TEST_CASE("buffered_sequence - with write_factory and single buffer", "[sio][buffered_sequence]") {
  auto ctx = exec::io_uring_context{};
  auto fd = create_memfd("with_write_factory");
  REQUIRE(fd != -1);

  auto factory = sio::io_uring::write_factory{&ctx, fd};
  const auto content = std::string{"hello world"};
  auto buffer = sio::buffer(content);
  auto buffered_write_some = sio::buffered_sequence(factory, buffer, 0);
  ::sync_wait(ctx, sio::ignore_all(std::move(buffered_write_some)));

  CHECK(read_file(fd) == content);
}
