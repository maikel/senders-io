#include "sio/sequence/fork.hpp"

#include "sio/sequence/empty_sequence.hpp"
#include "sio/sequence/any_sequence_of.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/repeat.hpp"

#include <catch2/catch.hpp>

template <class... Ts>
using just_item_t = decltype(stdexec::just(std::declval<Ts>()...));

TEST_CASE("fork - with empty_sequence") {
  auto fork = sio::fork(sio::empty_sequence());
  using sigs = stdexec::completion_signatures_of_t<decltype(fork), stdexec::empty_env>;
  using items = exec::item_types_of_t<decltype(fork), stdexec::empty_env>;
  using newSigs = stdexec::__concat_completion_signatures_t<stdexec::__with_exception_ptr, sigs>;
  sio::any_sequence_receiver_ref<newSigs>::any_sender<> seq = fork;
}

TEST_CASE("fork - with iterate") {
  std::array<int, 3> arr{1, 2, 3};
  auto iterate = sio::iterate(arr);
  auto fork = sio::fork(iterate);
  using sigs = stdexec::completion_signatures<stdexec::set_value_t(int), stdexec::set_stopped_t()>;
  using newSigs = stdexec::__concat_completion_signatures_t<stdexec::__with_exception_ptr, sigs>;
  sio::any_sequence_receiver_ref<newSigs>::any_sender<> seq = fork;
}

TEST_CASE("fork - with repeat") {
  auto repeat = sio::repeat(stdexec::just(42));
  auto fork = sio::fork(repeat);
  using sigs = stdexec::completion_signatures<
    stdexec::set_value_t(int),
    stdexec::set_stopped_t(),
    stdexec::set_error_t(std::exception_ptr)>;
  sio::any_sequence_receiver_ref<sigs>::any_sender<> seq = fork;
}