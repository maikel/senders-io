/*
 * Copyright (c) 2023 Maikel Nadolski
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

#include "../concepts.hpp"
#include "./sequence_concepts.hpp"

#include <exec/env.hpp>

namespace sio {
  namespace empty_sequence_ {
    using namespace stdexec;

    template <class Receiver>
    struct operation {
      [[no_unique_address]] Receiver rcvr_;

      void start(start_t) noexcept {
        stdexec::set_value(static_cast<Receiver&&>(rcvr_));
      }
    };

    struct sender {
      using is_sender = exec::sequence_tag;
      using completion_signatures = stdexec::completion_signatures<>;

      template <receiver_of<completion_signatures> Rcvr>
      operation<Rcvr> subscribe(exec::subscribe_t, Rcvr&& rcvr) const
        noexcept(nothrow_decay_copyable<Rcvr>) {
        return {static_cast<Rcvr&&>(rcvr)};
      }

      auto get_sequence_env(exec::get_sequence_env_t) const noexcept {
        return exec::make_env(
          exec::with(exec::parallelism, exec::lock_step),
          exec::with(exec::cardinality, std::integral_constant<std::size_t, 0>{}));
      }
    };

    struct empty_sequence_t {
      sender operator()() const noexcept {
        return {};
      }
    };
  }

  using empty_sequence_::empty_sequence_t;
  inline constexpr empty_sequence_t empty_sequence{};
}