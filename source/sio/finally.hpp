#pragma once

#include "./concepts.hpp"
#include "./sequence/sequence_concepts.hpp"

namespace sio {
  namespace finally_ {
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

      template <class Item>
      auto set_next(exec::set_next_t, Item&& item) {
        return exec::set_next(op_->receiver_, static_cast<Item&&>(item));
      }

      void set_value(stdexec::set_value_t) && noexcept {
        stdexec::start(op_->final_op_);
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept {
        // TODO store error
        stdexec::start(op_->final_op_);
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {

        stdexec::start(op_->final_op_);
      }
    };

    template <class InitialSender, class FinalSender, class Receiver>
    struct operation : operation_base<FinalSender, Receiver> {
      exec::subscribe_result_t<InitialSender, initial_receiver<FinalSender, Receiver>> first_op_;

      operation(InitialSender&& initial, FinalSender&& final, Receiver receiver)
        : operation_base<FinalSender, Receiver>(
          static_cast<FinalSender&&>(final),
          static_cast<Receiver&&>(receiver))
        , first_op_(exec::subscribe(
            std::forward<InitialSender>(initial),
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
      static auto get_item_types(Self&&, exec::get_item_types_t, Env&&)
        -> exec::item_types_of_t<copy_cvref_t<Self, InitialSender>, Env>;

      template <decays_to<sequence> Self, class Env>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> stdexec::make_completion_signatures<
          copy_cvref_t<Self, FinalSender>,
          Env,
          stdexec::completion_signatures<stdexec::set_stopped_t()>>;
    };

    struct finally_t {
      template <class Initial, class Final>
      sequence<decay_t<Initial>, decay_t<Final>> operator()(Initial&& init, Final&& final) const {
        return {static_cast<Initial&&>(init), static_cast<Final&&>(final)};
      }
    };
  }

  using finally_::finally_t;
  inline constexpr finally_t finally{};
}