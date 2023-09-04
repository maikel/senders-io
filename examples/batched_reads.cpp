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

int main(int argc, char *argv[]) {
  const std::size_t num_ints = argc >= 3 ? std::stoul(argv[2]) : 100000000;
  const std::size_t num_reads = argc >= 4 ? std::stoul(argv[3]) : 1000000;
  {
    exec::safe_file_descriptor fd{::memfd_create("test", 0)};
    if (argc >= 2)
      fd = exec::safe_file_descriptor{::open(argv[1], O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR)};
    {
      auto a = std::make_unique_for_overwrite<int[]>(num_ints);
      std::iota(a.get(), a.get() + num_ints, 0);
      
      const auto file_size = sizeof(a.get()[0]) * num_ints;
      if (::ftruncate(fd, file_size) != 0) {
        std::cerr << "file resize unsuccessful!\n";
        return -1;
      }
      if (::pwrite(fd, a.get(), file_size, 0) != file_size) {
        std::cerr << "file write unsuccessful!\n";
        return -1;
      }
    }
    exec::io_uring_context context{};
    sio::io_uring::native_fd_handle fdh{context, std::move(fd)};
    sio::io_uring::seekable_byte_stream stream{std::move(fdh)};
    using offset_type = sio::async::offset_type_of_t<decltype(stream)>;
    std::vector<offset_type> byte_offsets;
    std::vector<std::size_t> offsets, lens;
    std::mt19937_64 gen(1e9 + 7);
    constexpr int item_size = sizeof(int);
    constexpr int max_num_ints = 512;
    {
      std::uniform_int_distribution<std::size_t> off_dist(0, num_ints - 1);
      std::uniform_int_distribution<std::size_t> ints_dist(1, max_num_ints);
      for (int64_t i = 0; i < num_reads; i++) {
        offsets.push_back(off_dist(gen));
        const auto num_reads = ints_dist(gen);
        lens.push_back(std::min(num_ints - offsets.back(), num_reads));
        byte_offsets.push_back(offsets.back() * item_size);
      }
    }
    lens.push_back(0);
    std::exclusive_scan(lens.begin(), lens.end(), lens.begin(), 0);
    const auto total_len = lens.back();
    auto buffer = std::make_unique_for_overwrite<int[]>(total_len);
    auto buffer_data = buffer.get();
    std::vector<std::span<std::byte>> buffers;
    for (int64_t i = 0; i < num_reads; i++)
      buffers.push_back(std::as_writable_bytes(std::span{buffer_data + lens[i], buffer_data + lens[i + 1]}));
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
          std::cerr << "test failed in read " << i << ' ' << " and at index " << j - lens[i] << " !\n";
          std::cerr << buffer_data[j] << " != " << offsets[i] + j - lens[i] << '\n';
          // return -1;
        }
    }
    const auto avg_time = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    std::cout << "Read " << num_reads << " blocks of sizes upto " << max_num_ints * item_size
      << " bytes in time " << avg_time << "s for an average of " << num_reads / avg_time << " IOPS"
      << " and an average copy rate of " << total_len * item_size / avg_time / (1 << 30) << " GiB/s" << std::endl;
  }
  return 0;
}