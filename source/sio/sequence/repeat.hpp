#pragma once

#include "./sequence_concepts.hpp"

namespace sio {
  namespace repeat_ {
    template <class Sender, class Receiver>
    struct operation;

    template <class Sender, class Receiver>
    struct receiver {
      using is_receiver = void;
      operation<Sender, Receiver>* base_;

      stdexec::env_of_t<const Receiver&> get_env(stdexec::get_env_t) const noexcept;

      template <class Item>
      exec::next_sender_of_t<Receiver, Item> set_next(exec::set_next_t, Item&&);

      void set_value(stdexec::set_value_t) && noexcept;

      void set_stopped(stdexec::set_stopped_t) && noexcept;

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& error) && noexcept;
    };

    template <class Sender, class Receiver>
    struct operation {
      Sender sndr_;
      Receiver rcvr_;
      std::optional<exec::subscribe_result_t<const Sender&, receiver<Sender, Receiver>>> op_{};

      void repeat() noexcept {
        stdexec::queryable auto env = stdexec::get_env(rcvr_);
        auto token = stdexec::get_stop_token(env);
        if (token.stop_requested()) {
          stdexec::set_value(static_cast<Receiver&&>(rcvr_));
          return;
        }
        try {
          auto& op = op_.emplace(stdexec::__conv{[&] {
            return exec::subscribe((const Sender&) this->sndr_, receiver<Sender, Receiver>{this});
          }});
          stdexec::start(op);
        } catch (...) {
          stdexec::set_error(static_cast<Receiver&&>(rcvr_), std::current_exception());
        }
      }

      void start(stdexec::start_t) noexcept {
        repeat();
      }
    };

    template <class Sender, class Receiver>
    stdexec::env_of_t<const Receiver&>
      receiver<Sender, Receiver>::get_env(stdexec::get_env_t) const noexcept {
      return stdexec::get_env(base_->rcvr_);
    }

    template <class Sender, class Receiver>
    template <class Item>
    exec::next_sender_of_t<Receiver, Item>
      receiver<Sender, Receiver>::set_next(exec::set_next_t, Item&& item) {
      return exec::set_next(base_->rcvr_, static_cast<Item&&>(item));
    }

    template <class Sender, class Receiver>
    void receiver<Sender, Receiver>::set_value(stdexec::set_value_t) && noexcept {
      base_->repeat();
    }

    template <class Sender, class Receiver>
    void receiver<Sender, Receiver>::set_stopped(stdexec::set_stopped_t) && noexcept {
      exec::set_value_unless_stopped(static_cast<Receiver&&>(base_->rcvr_));
    }

    template <class Sender, class Receiver>
    template <class Error>
    void receiver<Sender, Receiver>::set_error(stdexec::set_error_t, Error&& error) && noexcept {
      stdexec::set_error(static_cast<Receiver&&>(base_->rcvr_), static_cast<Error&&>(error));
    }

    template <class Sender>
    struct sequence {
      using is_sender = exec::sequence_tag;

      template <class Self, class Env>
      static auto
        get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&) noexcept
        -> stdexec::__concat_completion_signatures_t<
          stdexec::__with_exception_ptr,
          stdexec::make_completion_signatures<
            Sender,
            Env,
            stdexec::completion_signatures<stdexec::set_stopped_t()>,
            stdexec::__mconst<stdexec::completion_signatures<>>::__f>>;

      template <class Self, class Env>
      static auto get_item_types(Self&&, exec::get_item_types_t, Env&&) noexcept
        -> exec::item_types_of_t<Sender, Env>;

      template <decays_to<sequence> Self, class Rcvr>
      static operation<Sender, Rcvr> subscribe(Self&& self, exec::subscribe_t, Rcvr rcvr) {
        return {static_cast<Self&&>(self).sndr_, static_cast<Rcvr&&>(rcvr)};
      }

      Sender sndr_;
    };
  }

  struct repeat_t {
    template <class Sender>
    repeat_::sequence<std::decay_t<Sender>> operator()(Sender&& sndr) const noexcept {
      return {static_cast<Sender&&>(sndr)};
    }
  };

  inline constexpr repeat_t repeat{};
}