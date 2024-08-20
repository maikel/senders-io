/*
 * Copyright (c) 2024 Maikel Nadolski
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
#pragma once

#include "./last.hpp"
#include "./scan.hpp"

#include <functional>

namespace sio {
  struct reduce_t {
    template <class Sender, class Tp, class Fn = std::plus<>>
    auto operator()(Sender&& sndr, Tp init, Fn fun = Fn()) const //
      -> stdexec::__well_formed_sender auto {
      return last(
        scan(static_cast<Sender&&>(sndr), static_cast<Tp&&>(init), static_cast<Fn&&>(fun)));
    }
  };

  inline constexpr reduce_t reduce{};
} // namespace sio
