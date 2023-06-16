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

#include "./async_resource.hpp"
#include "./concepts.hpp"

namespace sio {
  template <class IP>
  concept internet_protocol = requires(const IP& proto) {
    { proto.family() };
    { proto.type() };
    { proto.protocol() };
  };
}

namespace sio::async {
  namespace socket_ {
    struct socket_t;
  };

  using socket_::socket_t;
  extern const socket_t socket;

  namespace socket_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).socket(socket, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).socket(socket, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::socket(static_cast<Tp&&>(t), socket, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      {
        decay_t<Tp>::socket(static_cast<Tp&&>(t), socket, static_cast<Args&&>(args)...)
      } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct socket_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).socket(socket_t{}, static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::socket(
            static_cast<Tp&&>(t), socket_t{}, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const socket_t socket{};
  
  namespace connect_ {
    struct connect_t;
  };

  using connect_::connect_t;
  extern const connect_t connect;

  namespace connect_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).connect(connect, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).connect(connect, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::connect(static_cast<Tp&&>(t), connect, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      {
        decay_t<Tp>::connect(static_cast<Tp&&>(t), connect, static_cast<Args&&>(args)...)
      } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct connect_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).connect(connect_t{}, static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::connect(
            static_cast<Tp&&>(t), connect_t{}, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const connect_t connect{};
}