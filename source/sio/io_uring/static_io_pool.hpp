#pragma once

#include <exec/linux/io_uring_context.hpp>

#include <deque>
#include <span>

namespace sio {

  class static_io_pool {
   public:
    explicit static_io_pool(std::size_t nthreads);
    ~static_io_pool();

    void submit(exec::__io_uring::__task* task) noexcept;

    std::span<std::thread> threads() noexcept;

    void stop() noexcept;

   private:
    std::atomic<std::size_t> current_context_ = 0;
    std::deque<exec::io_uring_context> contexts_;
    std::vector<std::thread> threads_;
  };

}