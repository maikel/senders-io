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

#include "./concepts.hpp"
#include "./sequence/fork.hpp"
#include "./sequence/repeat.hpp"

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
      static_cast<Tp&&>(t).socket(static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).socket(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::socket(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::socket(static_cast<Tp&&>(t), static_cast<Args&&>(args)...) } noexcept;
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
          return static_cast<Tp&&>(t).socket(static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::socket(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
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
      static_cast<Tp&&>(t).connect(static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).connect(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::connect(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::connect(static_cast<Tp&&>(t), static_cast<Args&&>(args)...) } noexcept;
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
          return static_cast<Tp&&>(t).connect(static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::connect(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const connect_t connect{};

  namespace accept_once_ {
    struct accept_once_t;
  };

  using accept_once_::accept_once_t;
  extern const accept_once_t accept_once;

  namespace accept_once_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).accept_once(static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).accept_once(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::accept_once(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::accept_once(static_cast<Tp&&>(t), static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct accept_once_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).accept_once(static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::accept_once(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const accept_once_t accept_once{};

  namespace accept_ {
    struct accept_t;
  };

  using accept_::accept_t;
  extern const accept_t accept;

  namespace accept_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).accept(static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).accept(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::accept(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::accept(static_cast<Tp&&>(t), static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct accept_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...> || accept_once_::has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).accept(static_cast<Args&&>(args)...);
        } else if constexpr (has_static_member_cpo<Tp, Args...>) {
          return decay_t<Tp>::accept(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
        } else {
          return repeat(accept_once(static_cast<Tp&&>(t), static_cast<Args&&>(args)...)) //
               | fork();                                                                 //
        }
      }
    };
  }

  inline const accept_t accept{};

  namespace sendmsg_ {
    struct sendmsg_t;
  };

  using sendmsg_::sendmsg_t;
  extern const sendmsg_t sendmsg;

  namespace sendmsg_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).sendmsg(static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).sendmsg(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::sendmsg(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::sendmsg(static_cast<Tp&&>(t), static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct sendmsg_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).sendmsg(static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::sendmsg(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const sendmsg_t sendmsg{};

  namespace recvmsg_ {
    struct recvmsg_t;
  };

  using recvmsg_::recvmsg_t;
  extern const recvmsg_t recvmsg;

  namespace recvmsg_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).recvmsg(static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).recvmsg(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::recvmsg(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::recvmsg(static_cast<Tp&&>(t), static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct recvmsg_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).recvmsg(static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::recvmsg(static_cast<Tp&&>(t), static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const recvmsg_t recvmsg{};
}
