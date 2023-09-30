#include "./bachted_reads.hpp"

program_options::program_options(int argc, char** argv) {
  int c;
  int digit_optind = 0;

  while (1) {
    int this_option_optind = optind ? optind : 1;
    int option_index = 0;
    static struct option long_options[] = {
      {     "context", required_argument, 0, 'c'},
      {"queue-length", required_argument, 0, 'q'},
      {     "iodepth", required_argument, 0,   0},
      {    "buffered",       no_argument, 0, 'b'},
      {        "size", required_argument, 0, 's'},
      {     "verbose",       no_argument, 0,   0},
      {     "memfile", required_argument, 0, 'm'},
      {     "memsize", required_argument, 0, 'z'},
      {"mempool-size", required_argument, 0, 'p'},
      {        "help",       no_argument, 0, 'h'},
      {        "seed", required_argument, 0, 'r'},
      {     "threads", required_argument, 0, 't'},
      {             0,                 0, 0,   0}
    };

    const char* short_options = "b:s:m:hr:t:";

    c = getopt_long(argc, argv, short_options, long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'q':
      spmc_queue_length = std::stoi(optarg);
      break;

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

    case 'p': {
      std::string arg{optarg};
      std::size_t pos = 0;
      mempool_size = std::stoull(arg, &pos);
      if (pos < arg.size() && std::tolower(arg[pos]) == 'k') {
        mempool_size <<= 10;
        pos++;
      } else if (pos < arg.size() && std::tolower(arg[pos]) == 'm') {
        mempool_size <<= 20;
        pos++;
      } else if (pos < arg.size() && std::tolower(arg[pos]) == 'g') {
        mempool_size <<= 30;
        pos++;
      }
    } break;

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

  std::cout << "Statistics per thread:\n";
  std::cout << "  Thread ID | Bytes read | I/O operations\n";
  for (int i = 0; i < statistics.n_bytes_read.size(); i += counters::factor) {
    std::cout << std::setw(11) << std::setfill(' ') << i / counters::factor << " | ";
    std::cout << std::setw(10) << std::setfill(' ') << statistics.n_bytes_read[i].load() << " | ";
    std::cout << std::setw(14) << std::setfill(' ') << statistics.n_io_ops[i].load() << '\n';
  }

  return EXIT_SUCCESS;
}