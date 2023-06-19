#include "sio/memory_pool.hpp"
#include <catch2/catch.hpp>

TEST_CASE("memory_pool - empty and allocate", "[memory_pool]") {
  sio::memory_pool pool{};
  auto alloc = pool.allocate(1, 1)
             | stdexec::let_value([&pool](void* ptr) noexcept {
                 CHECK(ptr);
                 return pool.deallocate(ptr);
               });
  stdexec::sync_wait(alloc);
}