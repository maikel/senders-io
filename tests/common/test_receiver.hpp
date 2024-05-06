#pragma once

#include "sio/concepts.hpp"
#include <exec/sequence_senders.hpp>

struct any_sequence_receiver {
  using receiver_concept = stdexec::receiver_t;

  template <class Sender>
  friend auto tag_invoke(exec::set_next_t, any_sequence_receiver& self, Sender&& s) noexcept {
    return stdexec::just();
  }

  void set_value() && noexcept {
  }

  void set_stopped() && noexcept {
  }

  template <class E>
  void set_error(E&&) && noexcept {
  }

  auto get_env() const noexcept -> stdexec::empty_env {
    return {};
  }
};

struct any_receiver {
  using receiver_concept = stdexec::receiver_t;

  template <class... Arg>
  void set_value(Arg&&...) noexcept {
  }

  void set_stopped() noexcept {
  }

  template <class E>
  void set_error(E&&) noexcept {
  }
};

struct int_receiver {
  using receiver_concept = stdexec::receiver_t;

  void set_value(int) noexcept {
  }

  void set_stopped() noexcept {
  }

  template <class E>
  void set_error(E&&) noexcept {
  }

  auto get_env() const noexcept -> stdexec::empty_env {
    return {};
  }
};
