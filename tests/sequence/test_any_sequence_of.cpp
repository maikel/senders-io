#include "sio/sequence/any_sequence_of.hpp"
#include "sio/sequence/empty_sequence.hpp"
#include "sio/sequence/ignore_all.hpp"

#include <catch2/catch.hpp>

TEST_CASE("any_sequence_of - empty sequence", "[any_sequence_of][empty_sequence][ignore_all]") {
  sio::any_sequence_receiver_ref<stdexec::completion_signatures<stdexec::set_value_t()>>::any_sender<> sequence = sio::empty_sequence();
  CHECK(stdexec::sync_wait(sio::ignore_all(std::move(sequence))));
}