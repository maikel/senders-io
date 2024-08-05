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

#define STDEXEC_ENABLE_EXTRA_TYPE_CHECKING() 0

#include <stdexec/__detail/__domain.hpp>
#include <stdexec/execution.hpp>

namespace sio {
  template <class Fun, class... Args>
  concept callable = stdexec::__callable<Fun, Args...>;

  template <class Fun, class... Args>
  concept nothrow_callable = stdexec::__nothrow_callable<Fun, Args...>;

  template <class Fun, class... Args>
  using call_result_t = stdexec::__call_result_t<Fun, Args...>;

  template <class Ty>
  using decay_t = stdexec::__decay_t<Ty>;

  template <class Ty, class... Args>
  concept constructible_from = std::constructible_from<Ty, Args...>;

  template <class Ty, class... Args>
  concept nothrow_constructible_from = stdexec::__nothrow_constructible_from<Ty, Args...>;

  template <class Ty>
  concept nothrow_move_constructible = stdexec::__nothrow_constructible_from<Ty, Ty>;

  template <class Ty>
  concept nothrow_copy_constructible = stdexec::__nothrow_constructible_from<Ty, const Ty&>;

  template <class Ty>
  concept decay_copyable = stdexec::constructible_from<decay_t<Ty>, Ty>;

  template <class T1, class T2>
  concept decays_to = stdexec::__decays_to<T1, T2>;

  template <class Ty>
  concept nothrow_decay_copyable = stdexec::__nothrow_constructible_from<decay_t<Ty>, Ty>;

  template <class T1, class T2>
  using copy_cvref_t = stdexec::__copy_cvref_t<T1, T2>;

  template <class Fun, class... Args>
  using binder_back = stdexec::__binder_back<Fun, Args...>;

  template <class... Ts>
  using decayed_tuple = stdexec::__decayed_tuple<Ts...>;

  template <class Sender, class Receiver>
  concept nothrow_connectable = stdexec::__nothrow_connectable<Sender, Receiver>;

  template <class Variant, class Type, class... Args>
  concept emplaceable = requires(Variant& v, Args&&... args) {
    v.template emplace<Type>(static_cast<Args&&>(args)...);
  };

  template <class Variant, class Type, class... Args>
  concept nothrow_emplaceable = requires(Variant& v, Args&&... args) {
    { v.template emplace<Type>(static_cast<Args&&>(args)...) } noexcept;
  };
}
