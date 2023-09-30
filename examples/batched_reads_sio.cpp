#include "./bachted_reads.hpp"

namespace {
  using Context = sio::io_uring::io_uring_context;

  struct file_state {
    using stat_type = struct ::stat;

    explicit file_state(
      const file_options& fopts,
      Context& context,
      std::size_t memsize,
      std::size_t readn_n_bytes,
      std::size_t block_size,
      std::mt19937_64& rng,
      bool buffered) {

      // open file descriptor and get file size
      if (fopts.use_memfd) {
        fd = exec::safe_file_descriptor{::memfd_create(fopts.path.c_str(), 0)};
        throw_errno_if(!fd, "Creating memfd failed");
        throw_errno_if(::ftruncate(fd, memsize) == -1, "Calling ftruncate failed");
        file_size = memsize;
        num_blocks = memsize / block_size;
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
      stream = sio::io_uring::basic_seekable_byte_stream<Context>{
        sio::io_uring::basic_native_fd_handle<Context>{context, fd.native_handle()}
      };

      // Allocate buffers and offsets
      int read_num_blocks = readn_n_bytes / block_size;
      buffer_storage = make_unique_aligned(read_num_blocks * block_size, block_size);
      std::byte* buffer_data = buffer_storage.get();
      buffers.reserve(read_num_blocks);
      offsets.reserve(read_num_blocks);
      std::uniform_int_distribution<std::size_t> off_dist(0, num_blocks - 1);
      for (std::size_t i = 0; i < read_num_blocks; i++) {
        buffers.push_back(sio::mutable_buffer{buffer_data + i * block_size, block_size});
        offsets.push_back(off_dist(rng) * block_size);
      }
    }

    exec::safe_file_descriptor fd;
    sio::io_uring::basic_seekable_byte_stream<Context> stream;
    std::size_t file_size;
    std::size_t num_blocks;
    std::unique_ptr<std::byte[], aligned_deleter> buffer_storage{};
    std::vector<sio::mutable_buffer> buffers{};
    std::vector<::off_t> offsets{};
  };

  struct thread_state {
    explicit thread_state(
      std::span<const file_options> files,
      std::size_t mempool_size,
      std::size_t memsize,
      unsigned iodepth,
      std::size_t read_n_bytes,
      std::size_t block_size,
      bool buffered,
      std::mt19937_64& rng)
      : context{iodepth}
      , buffer(mempool_size)
      , upstream{(void*) buffer.data(), buffer.size()} {
      read_n_bytes /= files.size();
      read_n_bytes += (block_size - read_n_bytes % block_size) % block_size;
      for (const file_options& fopts: files) {
        this->files.emplace_back(fopts, context, memsize, read_n_bytes, block_size, rng, buffered);
      }
    }

    Context context;
    std::vector<file_state> files{};
    std::vector<std::byte> buffer{};
    monotonic_buffer_resource upstream;
    sio::memory_pool pool{&upstream};
  };

  void run_io_uring_sio_main(
    int thread_id,
    const program_options& options,
    std::span<const file_options> files,
    const std::size_t n_bytes_per_thread,
    counters& stats) {
    std::mt19937_64 rng{options.seed + thread_id};
    thread_state state(
      files,
      options.mempool_size,
      options.memsize,
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
    stdexec::sync_wait(exec::when_any(std::move(read_sender), state.context.run()));
    std::scoped_lock lock{stats.mtx};
    stats.n_completions += 1;
    stats.cv.notify_one();
  }
} // namespace

void run_io_uring_sio::operator()(const program_options& options, counters& stats) {
  std::vector<std::thread> threads{};
  int n_files_per_thread = options.files.size() / options.nthreads;
  if (n_files_per_thread < 1) {
    throw std::runtime_error{"Not enough files for the number of threads"};
  }
  std::size_t n_bytes_per_thread = options.n_total_bytes / options.nthreads;
  n_bytes_per_thread += (options.block_size - n_bytes_per_thread % options.block_size)
                      % options.block_size;
  for (int i = 0; i < options.nthreads; ++i) {
    std::span<const file_options> files{options.files};
    if (i == options.nthreads - 1) {
      files = files.subspan(i * n_files_per_thread);
    } else {
      files = files.subspan(i * n_files_per_thread, n_files_per_thread);
    }
    threads.emplace_back(
      run_io_uring_sio_main, i, std::ref(options), files, n_bytes_per_thread, std::ref(stats));
  }

  for (std::thread& thread: threads) {
    thread.join();
  }
}