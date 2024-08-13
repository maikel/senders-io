/*
 * Copyright (c) 2024 Maikel Nadolski
 * Copyright (c) 2024 Xiaoming Zhang
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/iterate.hpp"
#include "sio/sequence/repeat.hpp"

#include <catch2/catch_all.hpp>
#include <ranges>

#include <exec/sequence_senders.hpp>


// TODO: stack boom😂

// TEST_CASE("repeat - with ignore_all", "[sio][repeat]") {
//   auto repeat = sio::repeat(stdexec::just(42));
//   auto ignore = sio::ignore_all(std::move(repeat));
//   stdexec::sync_wait(std::move(ignore));
// }
//
// TEST_CASE("repeat - with iterate", "[sio][repeat][iterate]") {
//   std::array<int, 3> arr{1, 2, 3};
//   auto repeat = sio::repeat(sio::iterate(std::views::all(arr)));
//   auto ignore = sio::ignore_all(std::move(repeat));
//   stdexec::sync_wait(std::move(ignore));
// }
