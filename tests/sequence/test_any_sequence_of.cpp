#include "sio/sequence/any_sequence_of.hpp"
#include "sio/sequence/empty_sequence.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/first.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/then_each.hpp"

#include <ranges>

#include <catch2/catch.hpp>

template <class... Sigs>
using any_sequence_of = typename sio::any_sequence_receiver_ref<
  stdexec::completion_signatures<Sigs...>>::template any_sender<>;

using stdexec::set_value_t;
using stdexec::set_error_t;
using stdexec::set_stopped_t;

// TEST_CASE("any_sequence_of - empty sequence", "[any_sequence_of][empty_sequence][ignore_all]") {
//   any_sequence_of<> sequence = sio::empty_sequence();
//   CHECK(stdexec::sync_wait(sio::ignore_all(std::move(sequence))));
// }

// TEST_CASE("any_sequence_of - binds to just(0)", "[any_sequence_of][ignore_all]") {
//   any_sequence_of<set_value_t(int)> sequence = stdexec::just(0);
//   CHECK(stdexec::sync_wait(sio::ignore_all(std::move(sequence))));
// }

// TEST_CASE("any_sequence_of - binds to just()", "[any_sequence_of][ignore_all]") {
//   any_sequence_of<set_value_t()> sequence = stdexec::just();
//   CHECK(stdexec::sync_wait(sio::ignore_all(std::move(sequence))));
// }

// TEST_CASE(
//   "any_sequence_of - binds to just(42) and get back the result",
//   "[any_sequence_of][first]") {
//   any_sequence_of<set_value_t(int)> sequence = stdexec::just(42);
//   auto result = stdexec::sync_wait(sio::first(std::move(sequence)));
//   CHECK(result);
//   auto [x] = *result;
//   CHECK(x == 42);
// }

// TEST_CASE("any_sequence_of - with iterate", "[any_sequence_of][iterate][then_each][ignore_all]") {
//   any_sequence_of<set_value_t(int), set_stopped_t(), set_error_t(std::exception_ptr)> sequence =
//     sio::iterate(std::ranges::views::iota(0, 10));

//   int sum = 0;
//   auto compute_sum =                           //
//     std::move(sequence)                        //
//     | sio::then_each([&](int x) { sum += x; }) //
//     | sio::ignore_all();
//   CHECK(stdexec::sync_wait(std::move(compute_sum)));
//   CHECK(sum == 45);
// }