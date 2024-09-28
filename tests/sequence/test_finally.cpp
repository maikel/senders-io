/*
 * Copyright (c) 2024 Emmett Zhang
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

#include <catch2/catch_all.hpp>

#include "sio/sequence/finally.hpp"
#include "sio/sequence/then_each.hpp"
#include "sio/sequence/ignore_all.hpp"

TEST_CASE("finally - with then_each and ignore_all", "[sio][finally]") {
  auto sndr = sio::finally(stdexec::just(1), stdexec::just()) //
            | sio::then_each([&](int i) {                     //
                CHECK(i == 1);
              })
            | sio::ignore_all();
  stdexec::sync_wait(std::move(sndr));
}
