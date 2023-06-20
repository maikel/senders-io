#include "./memory_pool.hpp"

#include "./assert.hpp"

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace sio {

  // The following code is taken from stackoverflow
  // https://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers

  namespace {
    const int tab64[64] = {
      63, 0,  58, 1,  59, 47, 53, 2,  60, 39, 48, 27, 54, 33, 42, 3,  61, 51, 37, 40, 49, 18,
      28, 20, 55, 30, 34, 11, 43, 14, 22, 4,  62, 57, 46, 52, 38, 26, 32, 41, 50, 36, 17, 19,
      29, 10, 13, 21, 56, 45, 25, 31, 35, 16, 9,  12, 44, 24, 15, 8,  23, 7,  6,  5};

    int log2_64(std::size_t value) {
      value |= value >> 1;
      value |= value >> 2;
      value |= value >> 4;
      value |= value >> 8;
      value |= value >> 16;
      value |= value >> 32;
      return tab64[((uint64_t) ((value - (value >> 1)) * 0x07EDD5E59A4E28C2)) >> 58];
    }
  }

  memory_pool::memory_pool(std::pmr::memory_resource* res) noexcept
    : upstream_{res} {
    if (!upstream_) {
      upstream_ = std::pmr::get_default_resource();
    }
  }

  memory_pool::~memory_pool() {
    for (std::size_t i = 0; i < 32; ++i) {
      void* ptr = block_lists_[i];
      while (ptr) {
        void* next = nullptr;
        std::memcpy(&next, ptr, sizeof(void*));
        upstream_->deallocate(ptr, 1 << (i + 1));
        ptr = next;
      }
    }
  }

  allocate_sender memory_pool::allocate(std::size_t size, std::size_t) {
    auto index = log2_64(size + sizeof(memory_block));
    if (index < 0 || index >= 32) {
      throw std::invalid_argument("invalid size");
    }
    return allocate_sender{this, static_cast<std::size_t>(index)};
  }

  deallocate_sender memory_pool::deallocate(void* ptr) noexcept {
    return deallocate_sender{this, ptr};
  }

  void memory_pool::reclaim_memory(void* ptr) noexcept {
    if (!ptr) {
      return;
    }
    void* blockptr = static_cast<char*>(ptr) - sizeof(memory_block);
    memory_block block{};
    std::memcpy(&block, blockptr, sizeof(memory_block));
    SIO_ASSERT(block.index < 32);
    std::unique_lock lock(mutex_);
    if (!pending_allocation_[block.index].empty()) {
      allocate_operation_base* op = pending_allocation_[block.index].pop_front();
      op->result_.emplace<0>(ptr);
      lock.unlock();
      op->complete_(op);
    } else {
      block.next = block_lists_[block.index];
      std::memcpy(blockptr, &block, sizeof(memory_block));
      block_lists_[block.index] = blockptr;
    }
  }
}
