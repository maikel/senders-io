#include <sio/io_uring/file_handle.hpp>
#include <sio/read_batched.hpp>
#include <sio/sequence/reduce.hpp>
#include <sio/sequence/iterate.hpp>
#include <sio/sequence/then_each.hpp>
#include <sio/sequence/ignore_all.hpp>
#include <sio/memory_pool.hpp>
#include <sio/with_env.hpp>

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

void throw_errno_if(bool condition, const std::string& msg) {
  if (condition) {
    throw std::system_error{errno, std::system_category(), msg};
  }
}

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

std::unique_ptr<std::byte[], aligned_deleter>
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

struct file_state {
  using stat_type = struct ::stat;

  explicit file_state(
    const file_options& fopts,
    exec::io_uring_context& context,
    std::size_t readn_n_bytes,
    std::size_t block_size,
    std::mt19937_64& rng,
    bool buffered) {

    // open file descriptor and get file size
    if (fopts.use_memfd) {
      fd = exec::safe_file_descriptor{::memfd_create(fopts.path.c_str(), 0)};
      throw_errno_if(!fd, "Creating memfd failed");
      throw_errno_if(::ftruncate(fd, readn_n_bytes) == -1, "Calling ftruncate failed");
      file_size = readn_n_bytes;
      num_blocks = readn_n_bytes / block_size;
    } else {
      uint32_t flags = O_RDONLY | O_DIRECT;
      if (buffered) {
        flags = O_RDONLY;
      }
      fd = exec::safe_file_descriptor{::open(fopts.path.c_str(), flags)};
      throw_errno_if(!fd, "Opening '" + fopts.path + "' failed");
      stat_type st{};
      throw_errno_if(::fstat(fd, &st) == -1, "Calling fstat on '" + fopts.path + "' failed");
      if (S_ISBLK(st.st_mode)) {
        std::uint64_t n_bytes = 0;
        throw_errno_if(
          ::ioctl(fd, BLKGETSIZE64, &n_bytes) == -1, "Calling ioctl with BLKGETSIZE64 failed");
        file_size = n_bytes;
        num_blocks = n_bytes / block_size;
      } else if (S_ISREG(st.st_mode)) {
        file_size = st.st_size;
        num_blocks = st.st_size / block_size;
      } else {
        throw std::runtime_error{"Unsupported file type"};
      }
    }
    stream = sio::io_uring::seekable_byte_stream{
      sio::io_uring::native_fd_handle{context, fd.native_handle()}
    };

    // Allocate buffers and offsets
    int read_num_blocks = readn_n_bytes / block_size;
    buffer_storage = make_unique_aligned(read_num_blocks * block_size, block_size);
    std::byte* buffer_data = buffer_storage.get();
    buffers.reserve(read_num_blocks);
    offsets.reserve(read_num_blocks);
    std::uniform_int_distribution<std::size_t> off_dist(0, num_blocks - 1);
    for (std::size_t i = 0; i < read_num_blocks; i++) {
      buffers.push_back(
        std::as_writable_bytes(std::span{buffer_data + i * block_size, block_size}));
      offsets.push_back(off_dist(rng) * block_size);
    }
  }

  exec::safe_file_descriptor fd;
  sio::io_uring::seekable_byte_stream stream;
  std::size_t file_size;
  std::size_t num_blocks;
  std::unique_ptr<std::byte[], aligned_deleter> buffer_storage{};
  std::vector<std::span<std::byte>> buffers{};
  std::vector<::off_t> offsets{};
};

struct program_options {
  explicit program_options(int argc, char** argv) {
    int c;
    int digit_optind = 0;

    while (1) {
      int this_option_optind = optind ? optind : 1;
      int option_index = 0;
      static struct option long_options[] = {
        { "iodepth", required_argument, 0,   0},
        {"buffered",       no_argument, 0, 'b'},
        {    "size", required_argument, 0, 's'},
        { "verbose",       no_argument, 0,   0},
        { "memfile", required_argument, 0, 'm'},
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

      case 'm':
        files.emplace_back(file_options{optarg, true});
        break;

      case 'r':
        seed = std::stoi(optarg);
        break;

      case 's':
        n_total_bytes = std::stoull(optarg);
        n_total_bytes += (block_size - n_total_bytes % block_size);
        break;

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
};

struct thread_state {
  explicit thread_state(
    std::span<const file_options> files,
    unsigned iodepth,
    std::size_t read_n_bytes,
    std::size_t block_size,
    bool buffered,
    std::mt19937_64& rng)
    : context{iodepth}
    , buffer(2 * iodepth * (4 << 10))
    , upstream{buffer.data(), buffer.size(), never_alloc} {
    read_n_bytes /= files.size();
    read_n_bytes += (block_size - read_n_bytes % block_size);
    for (const file_options& fopts: files) {
      this->files.emplace_back(fopts, context, read_n_bytes, block_size, rng, buffered);
    }
  }

  exec::io_uring_context context{};
  std::vector<file_state> files{};
  std::vector<std::byte> buffer{};
  std::pmr::memory_resource* never_alloc = std::pmr::null_memory_resource();
  std::pmr::monotonic_buffer_resource upstream;
  sio::memory_pool pool{&upstream};
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
    auto n_bytes_read_ = std::accumulate(
      n_bytes_read.begin(), n_bytes_read.end(), std::size_t{}, [](auto a, auto& b) {
        return a + b.load(std::memory_order_relaxed);
      });
    auto n_io_ops_ = std::accumulate(
      n_io_ops.begin(), n_io_ops.end(), std::size_t{}, [](auto a, auto& b) {
        return a + b.load(std::memory_order_relaxed);
      });
    return std::make_pair(n_bytes_read_, n_io_ops_);
  };
};

auto read_with_counter(
  sio::io_uring::seekable_byte_stream stream,
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

void run_io_uring(
  const int thread_id,
  std::latch& barrier,
  const program_options& options,
  std::span<const file_options> files,
  const std::size_t n_bytes_per_thread,
  counters& stats) {
  std::mt19937_64 rng{options.seed + thread_id};
  thread_state state(
    files,
    options.submission_queue_length,
    n_bytes_per_thread,
    options.block_size,
    options.buffered,
    rng);
  namespace stdv = std::views;
  auto file_view =         //
    stdv::all(state.files) //
    | stdv::transform([](file_state& file) { return std::ref(file); });
  auto read_sender =
    sio::iterate(file_view) //
    | sio::fork()           //
    | sio::let_value_each([&](file_state& file) {
        sio::memory_pool_allocator<std::byte> allocator{&state.pool};
        return read_batched(file.stream, file.buffers, file.offsets, allocator, stats, thread_id);
      }) //
    | sio::ignore_all();
  barrier.arrive_and_wait();
  stdexec::sync_wait(exec::when_any(std::move(read_sender), state.context.run()));
  std::scoped_lock lock{stats.mtx};
  stats.n_completions += 1;
  stats.cv.notify_one();
}

template <class Duration>
std::ostream& print_time(Duration elapsed) {
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  auto s = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
  if (s > 0) {
    auto rest = ms % 1000;
    return std::cout << s << '.' << std::setw(3) << std::setfill('0') << rest << "s";
  } else if (ms > 0) {
    auto rest = us % 1000;
    return std::cout << ms << '.' << rest << "ms";
  } else if (us > 0) {
    auto rest = ns % 1000;
    return std::cout << us << '.' << rest << "us";
  }
  return std::cout << ns << "ns";
}

void print_statistics(const program_options& options, counters& statistics) {
  auto start = std::chrono::steady_clock::now();
  auto wait_for_completion = [&] {
    std::unique_lock lock{statistics.mtx};
    auto timeout = std::chrono::seconds{1};
    return statistics.cv.wait_for(lock, timeout, [&] {
      return statistics.n_completions == options.nthreads;
    });
  };
  while (!wait_for_completion()) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start);
    auto [n_bytes_read, n_io_ops] = statistics.load_stats();
    auto n_iops = n_io_ops / elapsed.count() * std::nano::den;
    auto n_bytes = n_bytes_read / elapsed.count() * std::nano::den;
    auto progress = n_bytes_read * 100 / options.n_total_bytes;
    std::cout << "\rRead " << n_io_ops << " blocks "
              << "(" << progress << "%) of size " << options.block_size << " bytes in time ";
    print_time(elapsed)
      << " for an average of " << n_iops << " IOPS"
      << " and an average copy rate of " << n_bytes / (1 << 20) << " MiB/s" << std::flush;
  }
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start);
  auto [n_bytes_read, n_io_ops] = statistics.load_stats();
  auto n_iops = n_io_ops / elapsed.count() * std::nano::den;
  auto n_bytes = n_bytes_read / elapsed.count() * std::nano::den;
  auto progress = n_bytes_read * 100 / options.n_total_bytes;
  std::cout << "\rRead " << n_io_ops << " blocks "
            << "(" << progress << "%) of size " << options.block_size << " bytes in time ";
  print_time(elapsed)
    << " for an average of " << n_iops << " IOPS"
    << " and an average copy rate of " << n_bytes / (1 << 20) << " MiB/s" << std::endl;
}

int main(int argc, char* argv[]) {
  program_options options(argc, argv);
  // libc++ has no jthread yet
  std::vector<std::thread> threads{};
  std::latch barrier{options.nthreads + 1};
  counters statistics{options.nthreads};
  int n_files_per_thread = options.files.size() / options.nthreads;
  if (n_files_per_thread < 1) {
    throw std::runtime_error{"Not enough files for the number of threads"};
  }
  std::size_t n_bytes_per_thread = options.n_total_bytes / options.nthreads;
  n_bytes_per_thread += (options.block_size - n_bytes_per_thread % options.block_size);
  for (int i = 0; i < options.nthreads; ++i) {
    std::span<const file_options> files{options.files};
    if (i == options.nthreads - 1) {
      files = files.subspan(i * n_files_per_thread);
    } else {
      files = files.subspan(i * n_files_per_thread, n_files_per_thread);
    }
    threads.emplace_back(
      run_io_uring,
      i,
      std::ref(barrier),
      std::ref(options),
      files,
      n_bytes_per_thread,
      std::ref(statistics));
  }

  barrier.arrive_and_wait();
  print_statistics(options, statistics);

  for (std::thread& t: threads) {
    t.join();
  }

  return 0;
}