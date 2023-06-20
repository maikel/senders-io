#include "sio/memory_pool.hpp"
#include <catch2/catch.hpp>

TEST_CASE("memory_pool - empty and allocate", "[memory_pool]") {
  sio::memory_pool pool{};
  auto alloc = pool.allocate(1, 1) | stdexec::let_value([&pool](void* ptr) noexcept {
                 CHECK(ptr);
                 return pool.deallocate(ptr);
               });
  stdexec::sync_wait(alloc);
}

TEST_CASE("memory_pool - bounded memory pool", "[memory_pool]") {
  char buffer[1024] = {};
  auto never_alloc = std::pmr::null_memory_resource();
  std::pmr::monotonic_buffer_resource upstream{buffer, sizeof(buffer), never_alloc};
  sio::memory_pool pool{&upstream};
  auto alloc_and_dealloc = pool.allocate(512, 1) | stdexec::let_value([&pool](void* ptr) noexcept {
                             CHECK(ptr);
                             return pool.deallocate(ptr);
                           });
  auto alloc =                                    //
    pool.allocate(512, alignof(std::max_align_t)) //
    | stdexec::let_value([&pool](void* ptr) noexcept {
        CHECK(ptr);
        return stdexec::just(ptr);
      })
    | stdexec::let_value([&pool, alloc_and_dealloc](void* ptr) noexcept {
        return stdexec::when_all(alloc_and_dealloc, pool.deallocate(ptr));
      });
  stdexec::sync_wait(alloc);
}