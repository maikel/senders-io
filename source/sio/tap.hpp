#pragma once

#include <stdexec/execution.hpp>

#include "./concepts.hpp"
#include "./sequence/sequence_concepts.hpp"

namespace sio {
  namespace tap_ {
    template <class Receiver>
    struct receiver_ref {
      Receiver& receiver_;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(receiver_);
      }

      void set_value(stdexec::set_value_t) && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(receiver_));
      }

      template <class Error>
        requires callable<stdexec::set_error_t, Receiver&&, Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(receiver_), static_cast<Error&&>(error));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept
        requires callable<stdexec::set_stopped_t, Receiver&&>
      {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(receiver_));
      }
    };

    template <class FinalSender, class Receiver>
    struct operation_base {
      Receiver receiver_;
      stdexec::connect_result_t<FinalSender, receiver_ref<Receiver>> final_op_;
      std::atomic<bool> success_{true};

      template <class InitialSender>
      auto make_initial_sender(InitialSender&& initial) {
        return exec::set_next(
          this->receiver_,
          stdexec::let_stopped(
            stdexec::let_error(
              std::forward<InitialSender>(initial),
              [this](auto error) {
                this->success_.store(false, std::memory_order_relaxed);
                return stdexec::just_error(std::move(error));
              }),
            [this] {
              this->success_.store(false, std::memory_order_relaxed);
              return stdexec::just_stopped();
            }));
      }

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
        if (op_->success_.load(std::memory_order_relaxed)) {
          stdexec::start(op_->final_op_);
        } else {
          exec::set_value_unless_stopped(std::move(op_->receiver_));
        }
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept
        requires callable<stdexec::set_stopped_t, Receiver&&>
      {
        if (op_->success_.load(std::memory_order_relaxed)) {
          stdexec::start(op_->final_op_);
        } else {
          exec::set_value_unless_stopped(std::move(op_->receiver_));
        }
      }
    };

    template <class InitialSender, class FinalSender, class Receiver>
    struct operation : operation_base<FinalSender, Receiver> {
      using next_sender_t =
        decltype(std::declval<operation_base<FinalSender, Receiver>&>().make_initial_sender(
          std::declval<InitialSender>()));

      stdexec::connect_result_t<next_sender_t, initial_receiver<FinalSender, Receiver>> first_op_;

      operation(InitialSender&& initial, FinalSender&& final, Receiver receiver)
        : operation_base<FinalSender, Receiver>(
          static_cast<FinalSender&&>(final),
          static_cast<Receiver&&>(receiver))
        , first_op_(stdexec::connect(
            this->make_initial_sender(static_cast<InitialSender&&>(initial)),
            initial_receiver<FinalSender, Receiver>{this})) {
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(first_op_);
      }
    };

    template <class InitialSender, class FinalSender>
    struct sequence {
      using is_sender = exec::sequence_tag;

      InitialSender initial_;
      FinalSender final_;

      template <decays_to<sequence> Self, stdexec::receiver Receiver>
      static auto subscribe(Self&& self, exec::subscribe_t, Receiver receiver)
        -> operation<copy_cvref_t<Self, InitialSender>, copy_cvref_t<Self, FinalSender>, Receiver> {
        return {
          static_cast<Self&&>(self).initial_,
          static_cast<Self&&>(self).final_,
          static_cast<Receiver&&>(receiver)};
      }

      template <decays_to<sequence> Self, class Env>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> stdexec::__concat_completion_signatures_t<
          stdexec::completion_signatures_of_t<copy_cvref_t<Self, InitialSender>, Env>,
          stdexec::__try_make_completion_signatures<
            FinalSender,
            Env,
            stdexec::completion_signatures<>,
            stdexec::__mconst<stdexec::completion_signatures<>>>>;
    };

    struct tap_t {
      template <class Initial, class Final>
      sequence<decay_t<Initial>, decay_t<Final>> operator()(Initial&& init, Final&& final) const {
        return {static_cast<Initial&&>(init), static_cast<Final&&>(final)};
      }
    };
  }

  using tap_::tap_t;
  inline constexpr tap_t tap{};
}