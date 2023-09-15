#include <sio/io_uring/file_handle.hpp>
#include <sio/io_uring/io_uring_context.hpp>
#include <sio/read_batched.hpp>
#include <sio/sequence/reduce.hpp>
#include <sio/sequence/iterate.hpp>
#include <sio/sequence/then_each.hpp>
#include <sio/sequence/ignore_all.hpp>
#include <sio/memory_pool.hpp>
#include <sio/with_env.hpp>
#include <sio/io_uring/static_thread_pool.hpp>

#include <exec/when_any.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <numeric>
#include <memory>
#include <random>
#include <ranges>
#include <string>
#include <thread>
#include <latch>
#include <new>
#include <map>
#include <string>
#include <functional>
#include <memory_resource>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/ioctl.h>

#ifdef __cpp_lib_hardware_interference_size
#if __cpp_lib_hardware_interference_size >= 201703
inline constexpr std::size_t hardware_destructive_interference_size =
  std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t hardware_destructive_interference_size = 2 * sizeof(std::max_align_t);
#endif
#else
inline constexpr std::size_t hardware_destructive_interference_size = 2 * sizeof(std::max_align_t);
#endif

#include <getopt.h>

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

inline void throw_errno_if(bool condition, const std::string& msg) {
  if (condition) {
    throw std::system_error{errno, std::system_category(), msg};
  }
}

struct aligned_deleter {
  void operator()(void* ptr) const noexcept {
    ::free(ptr);
  }
};

inline std::unique_ptr<std::byte[], aligned_deleter>
  make_unique_aligned(std::size_t size, std::size_t alignment) {
  void* ptr = nullptr;
  if (::posix_memalign(&ptr, alignment, size) != 0) {
    throw std::bad_alloc{};
  }
  return std::unique_ptr<std::byte[], aligned_deleter>{static_cast<std::byte*>(ptr)};
}

struct file_options {
  std::string path;
  bool use_memfd = false;
};

struct program_options {
  explicit program_options(int argc, char** argv) {
    int c;
    int digit_optind = 0;

    while (1) {
      int this_option_optind = optind ? optind : 1;
      int option_index = 0;
      static struct option long_options[] = {
        { "context", required_argument, 0, 'c'},
        { "iodepth", required_argument, 0,   0},
        {"buffered",       no_argument, 0, 'b'},
        {    "size", required_argument, 0, 's'},
        { "verbose",       no_argument, 0,   0},
        { "memfile", required_argument, 0, 'm'},
        { "memsize", required_argument, 0, 'z'},
        {    "help",       no_argument, 0, 'h'},
        {    "seed", required_argument, 0, 'r'},
        { "threads", required_argument, 0, 't'},
        {         0,                 0, 0,   0}
      };

      const char* short_options = "b:s:m:hr:t:";

      c = getopt_long(argc, argv, short_options, long_options, &option_index);
      if (c == -1)
        break;

      switch (c) {
      case 'b':
        buffered = true;
        break;

      case 'c':
        io_context = optarg;
        break;

      case 'm':
        files.emplace_back(file_options{optarg, true});
        break;

      case 'z':
        memsize = std::stoull(optarg);
        break;

      case 'r':
        seed = std::stoi(optarg);
        break;

      case 's': {
        std::string arg{optarg};
        std::size_t pos = 0;
        n_total_bytes = std::stoull(arg, &pos);
        if (pos < arg.size() && std::tolower(arg[pos]) == 'k') {
          n_total_bytes <<= 10;
          pos++;
        } else if (pos < arg.size() && std::tolower(arg[pos]) == 'm') {
          n_total_bytes <<= 20;
          pos++;
        } else if (pos < arg.size() && std::tolower(arg[pos]) == 'g') {
          n_total_bytes <<= 30;
          pos++;
        }
        n_total_bytes += (block_size - n_total_bytes % block_size) % block_size;
      } break;

      case 't':
        nthreads = std::stoi(optarg);
        break;
      case 'i':
        submission_queue_length = std::stoi(optarg);
        break;

      case 'h':
        [[fallthrough]];
      case '?':
        std::cout << R"EOF(Usage: batched_reads [OPTION]... [FILE]...

Command Line Options:
  --iodepth              Set the size of the submission queue
  -b, --buffered         Open file in buffered mode
  -s, --size=BYTES       Set the total number of bytes to process.
  -m, --memfile=FILE     Specify a memory file to be used.
  -z, --memsize=BYTES    Specify the size of the memory file.
  -r, --seed=SEED        Set the seed value for randomization.
  -t, --threads=THREADS  Set the number of threads to use.
  -h, --help             Display this help message and exit.

Arguments:
  FILE                   Optionally, one or more files to process.

Description:
  batched_reads is a command-line utility to measure the io performance of this library

Examples:
  1. Run the program that reads 1000000 bytes from /dev/sda:
     sudo batched_reads --size 1000000 /dev/sda

  2. Process a file named "data.txt" with buffering enabled
     batched_reads --buffered --size 1000000 data.txt

  3. Set the randomization seed to 42 and process a memory file named "memory_mapped":
     batched_reads -r 42 -m memory_mapped

  4. Run the program that reads 1000000 bytes from /dev/sda with two threads (both from /dev/sda)
     sudo batched_reads --size 1000000 --threads 2 /dev/sda /dev/sda

  5. Display this help message:
     batched_reads --help

Author:
  This program was written by Maikel Nadolski.

Report Bugs:
  Please report bugs to maikel.nadolski@gmail.com.
)EOF";
        exit(0);
        break;

      default:
        break;
      }
    }

    while (optind < argc) {
      files.push_back(file_options{argv[optind++], false});
    }
  }

  int nthreads = 1;
  std::size_t n_total_bytes = 4096;
  unsigned seed = 1e9 + 7;
  int block_size = 4096;
  std::uint32_t submission_queue_length = 1024;
  std::vector<file_options> files{};
  bool buffered = false;
  std::size_t memsize = 1 << 20;
  std::string io_context{"exec"};
};

struct counters {
  std::mutex mtx{};
  std::condition_variable cv{};
  int n_completions{};
  std::vector<std::atomic<std::size_t>> n_bytes_read;
  std::vector<std::atomic<std::size_t>> n_io_ops;
  static constexpr int factor = hardware_destructive_interference_size / sizeof(std::size_t);

  counters(int nthreads)
    : n_bytes_read(nthreads * factor)
    , n_io_ops(nthreads * factor) {
  }

  void notify_read(std::size_t n_bytes, int thread_id) {
    n_bytes_read[thread_id * factor].fetch_add(n_bytes, std::memory_order_relaxed);
    n_io_ops[thread_id * factor].fetch_add(1, std::memory_order_relaxed);
  }

  auto load_stats() {
    std::size_t n_bytes_read_ = 0, n_io_ops_ = 0;
    for (int i = 0; i < n_bytes_read.size(); i += factor) {
      n_bytes_read_ += n_bytes_read[i].load(std::memory_order_relaxed);
      n_io_ops_ += n_io_ops[i].load(std::memory_order_relaxed);
    }
    return std::make_pair(n_bytes_read_, n_io_ops_);
  };
};

auto read_with_counter(
  auto stream,
  std::span<std::byte> buffer,
  ::off_t offset,
  counters& stats,
  const int thread_id) {
  auto buffered_read_some = sio::buffered_sequence{sio::async::read_some(stream, buffer, offset)};
  auto with_increase_counters = sio::then_each(
    std::move(buffered_read_some), [&stats, thread_id](std::size_t n_bytes) noexcept {
      stats.notify_read(n_bytes, thread_id);
      return n_bytes;
    });
  return sio::reduce(std::move(with_increase_counters), std::size_t{0});
}

template <sio::async::seekable_byte_stream ByteStream>
auto read_batched(
  ByteStream stream,
  sio::async::buffers_type_of_t<ByteStream> buffers,
  std::span<const sio::async::offset_type_of_t<ByteStream>> offsets,
  sio::memory_pool_allocator<std::byte> allocator,
  counters& stats,
  const int thread_id) {
  auto env = exec::make_env(exec::with(sio::async::get_allocator, allocator));
  auto sender =
    sio::zip(sio::iterate(buffers), sio::iterate(offsets)) //
    | sio::fork()                                          //
    | sio::let_value_each([stream, &stats, thread_id](
                            sio::async::buffer_type_of_t<ByteStream> buffer,
                            sio::async::offset_type_of_t<ByteStream> offset) {
        return read_with_counter(stream, buffer, offset, stats, thread_id);
      })
    | sio::ignore_all();
  return sio::with_env(env, std::move(sender));
}

struct run_io_uring_exec {
  void operator()(const program_options& options, counters& stats);
};

struct run_io_uring_sio {
  void operator()(const program_options& options, counters& stats);
};

struct run_io_uring_pool {
  void operator()(const program_options& options, counters& stats);
};