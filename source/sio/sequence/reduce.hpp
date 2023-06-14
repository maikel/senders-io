#pragma once

#include "./last.hpp"
#include "./scan.hpp"

#include <functional>

namespace sio {
  struct reduce_t {
    template <class Sender, class Tp, class Fn = std::plus<>>
    auto operator()(Sender&& sndr, Tp init, Fn fun = Fn()) const {
      return last(scan(static_cast<Sender&&>(sndr), static_cast<Tp&&>(init), static_cast<Fn&&>(fun)));
    }
  };
  inline constexpr reduce_t reduce{};

} // namespace sio