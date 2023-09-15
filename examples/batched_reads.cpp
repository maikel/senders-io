#include "./bachted_reads.hpp"

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
    std::size_t n_iops = n_io_ops * 1.0 * std::nano::den / elapsed.count();
    std::size_t n_bytes = n_bytes_read * 1.0 * std::nano::den / elapsed.count();
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
  std::size_t n_iops = n_io_ops * 1.0 * std::nano::den / elapsed.count();
  std::size_t n_bytes = n_bytes_read * 1.0 * std::nano::den / elapsed.count();
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
  counters statistics{options.nthreads};

  std::map< std::string, std::function<void(const program_options&, counters&)>> run_map{
    {"exec", run_io_uring_exec{}},
    { "sio",  run_io_uring_sio{}},
    {"pool", run_io_uring_pool{}},
  };
  auto runner = run_map.find(options.io_context);
  if (runner == run_map.end()) {
    std::cerr << "Unknown io context '" << options.io_context << "'.\n";
    return EXIT_FAILURE;
  }

  std::thread worker_thread{runner->second, std::ref(options), std::ref(statistics)};
  exec::scope_guard guard{[&]() noexcept {
    worker_thread.join();
  }};
  print_statistics(options, statistics);


  return EXIT_SUCCESS;
}