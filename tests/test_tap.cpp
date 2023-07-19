#include "sio/tap.hpp"

#include "sio/sequence/then_each.hpp"
#include "sio/sequence/ignore_all.hpp"

#include "catch2/catch.hpp"

template <class F>
auto just_invoke(F f) {
  return stdexec::then(stdexec::just(), static_cast<F&&>(f));
}

TEST_CASE("tap - with senders") {
  int opened = 0;
  int closed = 0;
  auto tap = sio::tap(
    just_invoke([&] {
      opened++;
      return opened;
    }),
    just_invoke([&] { closed++; }));
  using tap_t = decltype(tap);
  STATIC_REQUIRE(stdexec::sender<tap_t>);
  STATIC_REQUIRE(exec::sequence_sender<tap_t>);
  CHECK(stdexec::sync_wait(
    sio::then_each(
      tap,
      [&](int i) {
        CHECK(i == 1);
        CHECK(i == opened);
        CHECK(closed == 0);
      })
    | sio::ignore_all()));
  CHECK(opened == 1);
  CHECK(closed == 1);
}