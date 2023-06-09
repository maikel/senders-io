#pragma once

#include "./assert.hpp"
#include "./concepts.hpp"

#include <variant>
#include <tuple>

namespace sio {
  template <class T>
  struct construct_t {
    template <class... Args>
    requires constructible_from<T, Args...>
    T operator()(Args&&... args) const noexcept(nothrow_constructible_from<T, Args...>) {
      return T(static_cast<Args&&>(args)...);
    }
  };

  template <class Tp, class Fn, class... Args>
  class deferred {
   private:
    std::variant<std::tuple<Args...>, Tp> data_;
    [[no_unique_address]] Fn fun_{};
   public:
    template <class... As>
      requires constructible_from<std::tuple<Args...>, As...>
    explicit deferred(As&&... args) noexcept(nothrow_constructible_from<std::tuple<Args...>, As...>)
      : data_(std::in_place_index<0>, static_cast<As&&>(args)...) {
    }

    template <class... As>
      requires constructible_from<std::tuple<Args...>, As...>
    explicit deferred(Fn fun, As&&... args) noexcept(
      nothrow_constructible_from<std::tuple<Args...>, As...>)
      : data_(std::in_place_index<0>, static_cast<As&&>(args)...)
      , fun_{static_cast<Fn&&>(fun)} {
    }

    deferred(deferred&& other) noexcept((nothrow_move_constructible<Args> && ...)) {
    }

    deferred(const deferred& other) noexcept((nothrow_copy_constructible<Args> && ...)) {
    }

    void operator()() noexcept(nothrow_constructible_from<Tp, call_result_t<Fn, Args...>>) {
      SIO_ASSERT(data_.index() == 0);
      std::tuple<Args...>& tup = *std::get_if<0>(&data_);
      data_.template emplace<1>(
        std::apply(
          [this]<class... As>(As&&... args) { return fun_(static_cast<As&&>(args)...); },
          static_cast<std::tuple<Args...>&&>(tup)));
    }

    Tp& operator*() noexcept {
      SIO_ASSERT(data_.index() == 1);
      return *std::get_if<1>(&data_);
    }

    const Tp& operator*() const noexcept {
      SIO_ASSERT(data_.index() == 1);
      return *std::get_if<1>(&data_);
    }
  };

  template <class Tp>
  struct make_deferred_t {
    template <class... Args>
    deferred<Tp, construct_t<Tp>, decay_t<Args>...> operator()(Args&&... args) const
      noexcept(nothrow_constructible_from<decayed_tuple<Args...>, Args...>) {
      return deferred<Tp, construct_t<Tp>, decay_t<Args>...>(static_cast<Args&&>(args)...);
    }
  };

  template <class Fn, class... Args>
  deferred<call_result_t<Fn, Args...>, Fn, decay_t<Args>...> defer(Fn fun, Args&&... args) {
    return deferred<call_result_t<Fn, Args...>, Fn, decay_t<Args>...>(
      static_cast<Fn&&>(fun), static_cast<Args&&>(args)...);
  }

  template <class Tp>
  inline constexpr make_deferred_t<Tp> make_deferred{};
}