#include <sio/io_uring/file_handle.hpp>
#include <sio/read_batched.hpp>
#include <sio/sequence/reduce.hpp>
#include <sio/sequence/iterate.hpp>
#include <sio/sequence/then_each.hpp>
#include <sio/sequence/ignore_all.hpp>

#include <exec/when_any.hpp>

#include <iostream>
#include <numeric>
#include <memory>
#include <random>
#include <string>
#include <cassert>
#include <chrono>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/unistd.h>

auto get_block_size(int fd) {
  struct stat fstat;
  ::fstat(fd, &fstat);
  auto blksize = fstat.st_blksize;
  return blksize;
}

struct aligned_deleter {
  void operator()(void* ptr) const noexcept {
    ::free(ptr);
  }
};

std::unique_ptr<int[], aligned_deleter>
  make_unique_aligned(std::size_t size, std::size_t alignment) {
  void* ptr = nullptr;
  if (::posix_memalign(&ptr, alignment, sizeof(int) * size) != 0) {
    throw std::bad_alloc{};
  }
  return std::unique_ptr<int[], aligned_deleter>{static_cast<int*>(ptr)};
}

int main(int argc, char* argv[]) {
  const std::size_t num_ints_tmp = argc >= 3 ? std::stoul(argv[2]) : 100000000;
  const std::size_t num_reads = argc >= 4 ? std::stoul(argv[3]) : 1000000;
  {
    exec::safe_file_descriptor fd{::memfd_create("test", 0)};
    if (argc >= 5) {
      fd = exec::safe_file_descriptor{::open(argv[4], O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR)};
    }
    auto block_size = get_block_size(fd);
    const std::size_t num_ints =
      (num_ints_tmp + (block_size / sizeof(int)) - 1)
      - (num_ints_tmp + (block_size / sizeof(int)) - 1) % (block_size / sizeof(int));
    if (!fd) {
      std::cerr << "file open unsuccessful: " << std::strerror(errno) << '\n';
      return -1;
    }
    {
      auto a = make_unique_aligned(num_ints, block_size);
      std::iota(a.get(), a.get() + num_ints, 0);

      const auto file_size = sizeof(a.get()[0]) * num_ints;
      if (::ftruncate(fd, file_size) != 0) {
        std::cerr << "file resize unsuccessful: " << std::strerror(errno) << '\n';
        return -1;
      }
      if (::pwrite(fd, a.get(), file_size, 0) != file_size) {
        std::cerr << "file write unsuccessful: " << std::strerror(errno) << '\n';
        return -1;
      }
    }
    unsigned submission_queue_length = 1024;
    if (argc >= 2) {
      submission_queue_length = std::stoul(argv[1]);
    }
    exec::io_uring_context context{submission_queue_length};
    sio::io_uring::native_fd_handle fdh{context, std::move(fd)};
    sio::io_uring::seekable_byte_stream stream{std::move(fdh)};
    using offset_type = sio::async::offset_type_of_t<decltype(stream)>;
    std::vector<offset_type> byte_offsets;
    std::vector<std::size_t> offsets, lens;
    std::mt19937_64 gen(1e9 + 7);
    constexpr int item_size = sizeof(int);
    {
      int int_block_size = block_size / item_size;
      int num_int_blocks = num_ints / int_block_size;
      std::uniform_int_distribution<std::size_t> off_dist(0, num_int_blocks - 1);
      for (int64_t i = 0; i < num_reads; i++) {
        offsets.push_back(off_dist(gen) * block_size);
        const int rest = std::max<int>(0, num_ints - offsets.back());
        const int length = std::min<int>(rest, int_block_size);
        lens.push_back(length);
        byte_offsets.push_back(offsets.back() * item_size);
      }
    }
    lens.push_back(0);
    std::exclusive_scan(lens.begin(), lens.end(), lens.begin(), 0);
    const auto total_len = lens.back();
    auto buffer = make_unique_aligned(total_len, block_size);
    auto buffer_data = buffer.get();
    std::vector<std::span<std::byte>> buffers;
    for (int64_t i = 0; i < num_reads; i++)
      buffers.push_back(
        std::as_writable_bytes(std::span{buffer_data + lens[i], buffer_data + lens[i + 1]}));
    std::vector<double> times;
    for (int i = 0; i < 1; i++) {
      auto sndr = sio::async::read_batched(stream, buffers, byte_offsets);
      const auto start = std::chrono::steady_clock::now();
      stdexec::sync_wait(exec::when_any(std::move(sndr), context.run()));
      const auto end = std::chrono::steady_clock::now();
      const std::chrono::duration<double> diff = end - start;
      times.push_back(diff.count());
    }
    for (int64_t i = 0; i < num_reads; i++) {
      for (auto j = lens[i]; j < lens[i + 1]; j++)
        if (buffer_data[j] != offsets[i] + j - lens[i]) {
          std::cerr << "test failed in read " << i << ' ' << " and at index " << j - lens[i]
                    << " !\n";
          std::cerr << buffer_data[j] << " != " << offsets[i] + j - lens[i] << '\n';
          return -1;
        }
    }
    const auto avg_time = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    const auto n_iops = num_reads / avg_time;
    const auto n_bytes = n_iops * block_size;
    std::cout << "Read " << num_reads << " blocks of size " << block_size << " bytes in time "
              << avg_time << "s for an average of " << num_reads / avg_time << " IOPS"
              << " and an average copy rate of " << n_bytes / (1 << 30) << " GiB/s" << std::endl;
  }
  return 0;
}