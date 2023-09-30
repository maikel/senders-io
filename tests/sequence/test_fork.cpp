#include "sio/sequence/fork.hpp"

#include "sio/sequence/empty_sequence.hpp"
#include "sio/sequence/any_sequence_of.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/repeat.hpp"
#include "sio/sequence/let_value_each.hpp"
#include "sio/sequence/then_each.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/zip.hpp"

#include "sio/io_uring/static_thread_pool.hpp"
#include "sio/io_uring/file_handle.hpp"
#include "sio/memory_pool.hpp"
#include "sio/with_env.hpp"

#include <exec/inline_scheduler.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>

#include <catch2/catch.hpp>

struct monotonic_buffer_resource : sio::memory_resource {
  monotonic_buffer_resource(void* buffer, std::size_t size) noexcept
    : memory_resource()
    , buffer_{buffer}
    , size_{size} {
  }

  void* buffer_;
  std::size_t size_;

  bool do_is_equal(const sio::memory_resource& other) const noexcept override {
    return this == &other;
  }

  void* do_allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    void* ptr = std::align(alignment, bytes, buffer_, size_);
    if (ptr && bytes <= size_) {
      size_ -= bytes;
      buffer_ = static_cast<char*>(buffer_) + bytes;
    }
    return ptr;
  }

  void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) noexcept override {
  }
};

TEST_CASE("fork - with empty_sequence", "[fork]") {
  auto fork = sio::fork(sio::empty_sequence());
  using sigs = stdexec::completion_signatures_of_t<decltype(fork), stdexec::empty_env>;
  using items = exec::item_types_of_t<decltype(fork), stdexec::empty_env>;
  using newSigs = stdexec::__concat_completion_signatures_t<stdexec::__with_exception_ptr, sigs>;
  sio::any_sequence_receiver_ref<newSigs>::any_sender<> seq = fork;
}

TEST_CASE("fork - with iterate", "[fork]") {
  std::array<int, 3> arr{1, 2, 3};
  auto iterate = sio::iterate(arr);
  auto fork = sio::fork(iterate);
  using sigs = stdexec::completion_signatures<stdexec::set_value_t(int), stdexec::set_stopped_t()>;
  using newSigs = stdexec::__concat_completion_signatures_t<stdexec::__with_exception_ptr, sigs>;
  sio::any_sequence_receiver_ref<newSigs>::any_sender<> seq = fork;
  CHECK(true);
}

TEST_CASE("fork - with repeat", "[fork]") {
  auto repeat = sio::repeat(stdexec::just(42));
  auto fork = sio::fork(repeat);
  using sigs = stdexec::completion_signatures<
    stdexec::set_value_t(int),
    stdexec::set_stopped_t(),
    stdexec::set_error_t(std::exception_ptr)>;
  sio::any_sequence_receiver_ref<sigs>::any_sender<> seq = fork;
  CHECK(true);
}

namespace stdv = std::views;

template <sio::async::seekable_byte_stream ByteStream>
auto read_batched(
  ByteStream stream,
  std::span<sio::async::buffer_type_of_t<ByteStream>> buffers,
  std::span<const sio::async::offset_type_of_t<ByteStream>> offsets) {
  auto sender =
    sio::zip(sio::iterate(buffers), sio::iterate(offsets)) //
    | sio::fork()                                          //
    | sio::let_value_each([stream](
                            sio::async::buffer_type_of_t<ByteStream> buffer,
                            sio::async::offset_type_of_t<ByteStream> offset) {
        return sio::async::read(stream, buffer, offset);
      })
    | sio::ignore_all();
  return sender;
}

using native_fd_handle = sio::io_uring::basic_native_fd_handle<sio::io_uring::static_thread_pool>;
using stream = sio::io_uring::basic_seekable_byte_stream<sio::io_uring::static_thread_pool>;

TEST_CASE("fork - thread safety", "[fork]") {
  exec::safe_file_descriptor fd(::memfd_create("test", MFD_CLOEXEC));
  REQUIRE(::ftruncate(fd, 10 * (4 << 10)) == 0);
  sio::io_uring::static_thread_pool ctx(2);
  native_fd_handle h(ctx, fd);
  stream strm(h);
  std::vector<std::byte> read_buffer(10 * (4 << 10));
  std::vector<sio::mutable_buffer> buffers{};
  std::vector<sio::async::offset_type_of_t<stream>> offsets{};
  for (int i = 0; i < 10; ++i) {
    buffers.push_back(sio::mutable_buffer(read_buffer.data() + (i * (4 << 10)), 4 << 10));
    offsets.push_back(i * (4 << 10));
  }
  REQUIRE(fd);
  std::vector<std::byte> mem_buffer(1 * (1 << 10));
  monotonic_buffer_resource upstream{(void*) mem_buffer.data(), mem_buffer.size()};
  sio::memory_pool pool{&upstream};
  auto fork =       //
    stdexec::just() //
    | sio::fork()   //
    | sio::let_value_each([&]() {
        sio::memory_pool_allocator<std::byte> allocator{&pool};
        auto env = exec::make_env(exec::with(sio::async::get_allocator, allocator));
        return sio::with_env(env, read_batched(strm, buffers, offsets));
      }) //
    | sio::ignore_all();
  auto result = stdexec::sync_wait(fork);
  CHECK(result);
}

TEST_CASE("fork - thread safety io pool", "[fork]") {
  exec::single_thread_context pool{};
  auto scheduler = pool.get_scheduler();
  std::array<int, 2> arr{1, 2};
  auto just_one = stdexec::just(1);
  auto fork =                                                                                   //
    sio::iterate(stdv::all(arr))                                                                //
    | sio::fork()                                                                               //
    | sio::let_value_each([=](int i) { return stdexec::on(scheduler, stdexec::just(i * 42)); }) //
    | sio::ignore_all();
  auto result = stdexec::sync_wait(std::move(fork));
  CHECK(result);
}