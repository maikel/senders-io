#pragma once

#include <exec/when_any.hpp>
#include <exec/timed_scheduler.hpp>

#include "./concepts.hpp"

#include <iostream>

namespace sio {

  template <class Sender, class Duration>
  concept timeable_sender =
    callable<stdexec::get_completion_scheduler_t<stdexec::set_value_t>, stdexec::env_of_t<Sender>>
    && exec::timed_scheduler<call_result_t<
      stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
      stdexec::env_of_t<Sender>>>
    && std::convertible_to<
      Duration,
      exec::duration_of_t<call_result_t<
        stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
        stdexec::env_of_t<Sender>>>>;

  struct timeout_t {
    template <exec::timed_scheduler CompletionScheduler, class Sender>
    auto operator()(
      CompletionScheduler sched,
      Sender&& sndr,
      exec::duration_of_t<CompletionScheduler> timeout) const {
      return exec::when_any(
        static_cast<Sender&&>(sndr),
        exec::schedule_after(sched, timeout) | stdexec::let_value([] {
          return stdexec::just_error(
            std::make_exception_ptr(std::system_error{std::make_error_code(std::errc::timed_out)}));
        }));
    }

    template <class Sender, class Duration>
      requires timeable_sender<Sender, Duration>
    auto operator()(Sender&& sndr, Duration timeout) const {
      return this->operator()(
        stdexec::get_completion_scheduler<stdexec::set_value_t>(stdexec::get_env(sndr)),
        static_cast<Sender&&>(sndr),
        timeout);
    }
  };

  inline constexpr timeout_t timeout{};
}