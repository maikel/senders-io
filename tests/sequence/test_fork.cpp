#include <catch2/catch_all.hpp>
#include <exec/sequence/ignore_all_values.hpp>
#include <exec/sequence_senders.hpp>

#include "sio/sequence/first.hpp"
#include "sio/sequence/fork.hpp"
#include "sio/sequence/any_sequence_of.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/last.hpp"
#include "sio/sequence/then_each.hpp"
#include "sio/sequence/let_value_each.hpp"

TEST_CASE("fork - with iterate", "[sio][fork]") {
  std::array<int, 3> arr{1, 2, 3};
  auto iterate = sio::iterate(arr);
  auto fork = sio::fork(std::move(iterate));
  using sigs = stdexec::completion_signatures<stdexec::set_value_t(int), stdexec::set_stopped_t()>;
  using newSigs = stdexec::__concat_completion_signatures<stdexec::__eptr_completion, sigs>;
  sio::any_sequence_receiver_ref<newSigs>::any_sender<> seq = fork;
}

TEST_CASE("fork - with iterate and ignore_all", "[sio][fork]") {
  std::array<double, 3> arr{1.0, 2, 3};
  auto sndr = sio::iterate(arr) //
            | sio::fork()       //
            | sio::ignore_all();
  stdexec::sync_wait(std::move(sndr));
}

TEST_CASE("fork - with iterate and first", "[sio][fork]") {
  std::array<int, 3> arr{1, 2, 3};
  auto sndr = sio::iterate(std::views::all(arr)) //
            | sio::fork()                        //
            | sio::first();
  stdexec::sync_wait(std::move(sndr)).value();
}

TEST_CASE("fork - with iterate and last", "[sio][fork]") {
  std::array<int, 3> arr{1, 2, 3};
  auto sndr = sio::iterate(std::views::all(arr)) //
            | sio::fork()                        //
            | sio::last();
  stdexec::sync_wait(std::move(sndr)).value();
}

TEST_CASE("fork - with then_each and ignore_all", "[sio][fork]") {
  int count = 1;
  std::array<int, 3> arr{1, 2, 3};
  auto sndr = sio::iterate(std::views::all(arr))                  //
            | sio::fork()                                         //
            | sio::then_each([&](int i) { CHECK(i == count++); }) //
            | sio::ignore_all();
  stdexec::sync_wait(std::move(sndr));
}

TEST_CASE("fork - a compilcated case", "[zip][iterate][fork]") {
  std::array<int, 2> arr{42, 43};

  auto sender = sio::iterate(arr) //
              | sio::fork()       //
              | sio::let_value_each([arr](int) {
                  return sio::iterate(arr)                                                //
                       | sio::fork()                                                      //
                       | sio::let_value_each([](int) { return stdexec::just_stopped(); }) //
                       | sio::ignore_all();
                })
              | sio::ignore_all();
  stdexec::sync_wait(std::move(sender));
}

// TODO: Stack overflow
// TEST_CASE("fork - with repeat and just_stopped", "[sio][fork]") {
//   auto sndr = sio::repeat(stdexec::just_stopped()) //
//             | sio::fork()                          //
//             | sio::ignore_all();
//   stdexec::sync_wait(std::move(sndr));
// }
//
// TEST_CASE("fork - with repeat and ignore_all", "[sio][fork]") {
//   auto sndr = sio::repeat(stdexec::just(42)) //
//             | sio::fork()                    //
//             | sio::ignore_all();
//   stdexec::sync_wait(std::move(sndr));
// }

// TEST_CASE("fork - with empty_sequence") {
//   auto fork = sio::fork(sio::empty_sequence());
//   using sigs = stdexec::completion_signatures_of_t<decltype(fork), stdexec::empty_env>;
//   using items = exec::item_types_of_t<decltype(fork), stdexec::empty_env>;
//   using newSigs = stdexec::__concat_completion_signatures<stdexec::__with_exception_ptr, sigs>;
//   sio::any_sequence_receiver_ref<newSigs>::any_sender<> seq = fork;
// }
