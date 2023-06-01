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

#include "./transform_each.hpp"

namespace sio {
  struct let_value_each_t {
    template <class Sender, class Fn>
      requires stdexec::tag_invocable<let_value_each_t, Sender, Fn>
    auto operator()(Sender&& sender, Fn&& fn) const
      noexcept(stdexec::nothrow_tag_invocable<let_value_each_t, Sender, Fn>)
        -> stdexec::tag_invoke_result_t<let_value_each_t, Sender, Fn> {
      return stdexec::tag_invoke(*this, static_cast<Sender&&>(sender), static_cast<Fn&&>(fn));
    }

    template <class Sender, class Fn>
      requires(!stdexec::tag_invocable<let_value_each_t, Sender, Fn>)
    auto operator()(Sender&& sender, Fn&& fn) const {
      return sio::transform_each(
        static_cast<Sender&&>(sender), stdexec::let_value(static_cast<Fn&&>(fn)));
    }

    template <class Fn>
    auto operator()(Fn&& fn) const -> binder_back<let_value_each_t, Fn> {
      return {{}, {}, {static_cast<Fn&&>(fn)}};
    }
  };

  inline constexpr let_value_each_t let_value_each{};
}