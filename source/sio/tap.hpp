#pragma once

#include <stdexec/execution.hpp>

#include "./concepts.hpp"

namespace sio {
  namespace tap_ {
    template <class Receiver>
    struct receiver_ref {
      Receiver& receiver_;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(receiver_);
      }

      template <class... Args>
        requires callable<stdexec::set_value_t, Receiver&&, Args...>
      void set_value(stdexec::set_value_t, Args&&... args) && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(receiver_), static_cast<Args&&>(args)...);
      }

      template <class Error>
        requires callable<stdexec::set_error_t, Receiver&&, Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(receiver_), static_cast<Error&&>(error));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept
        requires callable<stdexec::set_stopped_t, Receiver&&>
      {
        stdexec::set_stopped(static_cast<Receiver&&>(receiver_));
      }
    };

    template <class FinalSender, class Receiver>
    struct operation_base {
      Receiver receiver_;
      stdexec::connect_result_t<FinalSender, receiver_ref<Receiver>> final_op_;

      operation_base(FinalSender&& final, Receiver receiver)
        : receiver_{static_cast<Receiver&&>(receiver)}
        , final_op_(stdexec::connect(
            static_cast<FinalSender&&>(final),
            receiver_ref<Receiver>{receiver_})) {
      }
    };

    template <class FinalSender, class Receiver>
    struct initial_receiver {
      using is_receiver = void;
      operation_base<FinalSender, Receiver>* op_;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->receiver_);
      }

      void set_value(stdexec::set_value_t) && noexcept {
        stdexec::start(op_->final_op_);
      }

      template <class Error>
        requires callable<stdexec::set_error_t, Receiver&&, Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->receiver_), static_cast<Error&&>(error));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept
        requires callable<stdexec::set_stopped_t, Receiver&&>
      {
        stdexec::set_stopped(static_cast<Receiver&&>(op_->receiver_));
      }
    };

    template <class InitialSender, class FinalSender, class Receiver>
    struct operation : operation_base<FinalSender, Receiver> {
      stdexec::connect_result_t<InitialSender, initial_receiver<FinalSender, Receiver>> first_op_;

      operation(InitialSender&& initial, FinalSender&& final, Receiver receiver)
        : operation_base<FinalSender, Receiver>(
          static_cast<FinalSender&&>(final),
          static_cast<Receiver&&>(receiver))
        , first_op_(stdexec::connect(
            static_cast<InitialSender&&>(initial),
            initial_receiver<FinalSender, Receiver>{this})) {
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(first_op_);
      }
    };

    template <class InitialSender, class FinalSender>
    struct sender {
      using is_sender = void;

      InitialSender initial_;
      FinalSender final_;

      template <decays_to<sender> Self, stdexec::receiver Receiver>
      static auto connect(Self&& self, stdexec::connect_t, Receiver receiver)
        -> operation<copy_cvref_t<Self, InitialSender>, copy_cvref_t<Self, FinalSender>, Receiver> {
        return {
          static_cast<Self&&>(self).initial_,
          static_cast<Self&&>(self).final_,
          static_cast<Receiver&&>(receiver)};
      }

      template <decays_to<sender> Self, class Env>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> stdexec::__concat_completion_signatures_t<
          stdexec::completion_signatures_of_t<copy_cvref_t<Self, FinalSender>, Env>,
          stdexec::__try_make_completion_signatures<
            InitialSender,
            Env,
            stdexec::completion_signatures<>,
            stdexec::__mconst<stdexec::completion_signatures<>>>>;
    };

    struct tap_t {
      template <class Initial, class Final>
      sender<decay_t<Initial>, decay_t<Final>> operator()(Initial&& init, Final&& final) const {
        return {static_cast<Initial&&>(init), static_cast<Final&&>(final)};
      }
    };
  }

  using tap_::tap_t;
  inline constexpr tap_t tap{};
}