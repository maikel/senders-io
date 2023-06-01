#pragma once

#include "sio/concepts.hpp"

struct any_receiver {
  template <class Sender>
  auto set_next(exec::set_next_t, Sender&&) noexcept {
    return stdexec::just();
  }

  void set_value(stdexec::set_value_t) && noexcept {
  }

  void set_stopped(stdexec::set_stopped_t) && noexcept {
  }

  template <class E>
  void set_error(stdexec::set_error_t, E&&) && noexcept {
  }

  stdexec::empty_env get_env(stdexec::get_env_t) const noexcept {
    return {};
  }
};