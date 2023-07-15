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

  template <class Tp, class... Args>
  class deferred {
   private:
    union storage {
      std::tuple<Args...> args;
      Tp value;

      template <class... As>
      storage(As&&... args)
        : args(static_cast<As&&>(args)...) {
      }

      storage(const storage& other)
        : args{other.args} {
      }

      storage(storage&& other) noexcept
        : args{std::move(other.args)} {
      }

      storage& operator=(const storage& other) = delete;
      storage& operator=(storage&&) noexcept = delete;

      ~storage() {
      }
    } data_;

    bool emplaced_;
   public:
    template <class... As>
      requires constructible_from<std::tuple<Args...>, As...>
    explicit deferred(As&&... args) noexcept(nothrow_constructible_from<std::tuple<Args...>, As...>)
      : data_(static_cast<As&&>(args)...) {
    }

    deferred(deferred&& other) noexcept((nothrow_move_constructible<Args> && ...))
      : data_{static_cast<storage&&>(other.data_)} {
    }

    deferred& operator=(deferred&&) = delete;

    deferred(const deferred& other) noexcept((nothrow_copy_constructible<Args> && ...))
      : data_{other.data_} {
    }

    ~deferred() {
      if (emplaced_) {
        std::destroy_at(&data_.value);
      } else {
        std::destroy_at(&data_.args);
      }
    }

    deferred& operator=(const deferred&) = delete;

    void operator()() noexcept(nothrow_constructible_from<Tp, Args...>) {
      std::tuple<Args...> tup = static_cast<std::tuple<Args...>&&>(data_.args);
      std::destroy_at(&data_.args);
      std::apply(
        [this]<class... As>(As&&... args) {
          return std::construct_at(&data_.value, static_cast<As&&>(args)...);
        },
        static_cast<std::tuple<Args...>&&>(tup));
    }

    Tp& operator*() noexcept {
      return data_.value;
    }

    const Tp& operator*() const noexcept {
      return data_.value;
    }
  };

  template <class Tp>
  struct make_deferred_t {
    template <class... Args>
    deferred<Tp, Args...> operator()(Args&&... args) const
      noexcept(nothrow_constructible_from<decayed_tuple<Args...>, Args...>) {
      return deferred<Tp, Args...>(static_cast<Args&&>(args)...);
    }
  };

  template <class Tp>
  inline constexpr make_deferred_t<Tp> make_deferred{};
}