#pragma once

#include <stdexec/execution.hpp>

namespace ex = stdexec;
using eptr = std::exception_ptr;

template <class Haystack>
struct mall_contained_in {
  template <class... Needles>
  using __f = ex::__mand<ex::__mapply<ex::__mcontains<Needles>, Haystack>...>;
};

template <class Needles, class Haystack>
concept all_contained_in = ex::__v<ex::__mapply<mall_contained_in<Haystack>, Needles>>;

template <class Needles, class Haystack>
concept set_equivalent =
  ex::same_as<ex::__mapply<ex::__msize, Needles>, ex::__mapply<ex::__msize, Haystack>>
  && all_contained_in<Needles, Haystack>;
