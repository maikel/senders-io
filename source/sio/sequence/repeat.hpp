#pragma once

#include "./sequence_concepts.hpp"

#include <exec/__detail/__basic_sequence.hpp>
#include <stdexec/functional.hpp>

namespace sio {
  namespace repeat_ {
    template <class Sender, class Receiver>
    struct operation;

    template <class Sender, class Receiver>
    struct receiver {
      using receiver_concept = stdexec::receiver_t;

      operation<Sender, Receiver>* base_;

      template <class Item>
      friend auto tag_invoke(exec::set_next_t, receiver& self, Item&& item)
        -> exec::next_sender_of_t<Receiver, Item> {
        return exec::set_next(self.base_->rcvr_, static_cast<Item&&>(item));
      }

      void set_value() && noexcept {
        base_->repeat();
      }

      void set_stopped() && noexcept {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(base_->rcvr_));
      }

      template <class Error>
      void set_error(Error&& error) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(base_->rcvr_), static_cast<Error&&>(error));
      }

      auto get_env() const noexcept -> stdexec::env_of_t<const Receiver&> {
        return stdexec::get_env(base_->rcvr_);
      }
    };

    template <class Sender, class Receiver>
    struct operation {
      using receiver_t = repeat_::receiver<Sender, Receiver>;
      using subscribe_result_t = exec::subscribe_result_t<const Sender&, receiver_t >;

      Sender sndr_;
      Receiver rcvr_;
      std::optional<subscribe_result_t> op_{};

      void repeat() noexcept {
        stdexec::queryable auto env = stdexec::get_env(this->rcvr_);
        auto token = stdexec::get_stop_token(env);
        if (token.stop_requested()) {
          stdexec::set_value(static_cast<Receiver&&>(this->rcvr_));
          return;
        }
        try {
          auto& op = op_.emplace(stdexec::__conv{[&] {
            return exec::subscribe((const Sender&) this->sndr_, receiver_t{this});
          }});
          stdexec::start(op);
        } catch (...) {
          stdexec::set_error(static_cast<Receiver&&>(this->rcvr_), std::current_exception());
        }
      }

      void start() noexcept {
        repeat();
      }
    };

    template <class Receiver>
    struct subscribe_fn {
      Receiver& rcvr;

      template <class Child>
        requires exec::sequence_sender_to<Child, receiver<Child, Receiver>>
      auto operator()(stdexec::__ignore, stdexec::__ignore, Child&& child) const noexcept
        -> operation<Child, Receiver> {
        return {static_cast<Child&&>(child), static_cast<Receiver&&>(rcvr)};
      }
    };

    struct repeat_t {
      template <stdexec::sender Sender>
      auto operator()(Sender&& sndr) const noexcept -> stdexec::__well_formed_sender auto {
        auto domain = stdexec::__get_early_domain(static_cast<Sender&&>(sndr));
        return stdexec::transform_sender(
          domain, exec::make_sequence_expr<repeat_t>(stdexec::__{}, static_cast<Sender&&>(sndr)));
      }

      template <stdexec::sender_expr_for<repeat_t> Self, class Env>
      static auto get_completion_signatures(Self&&, Env&&) noexcept
        -> stdexec::__concat_completion_signatures<
          stdexec::__eptr_completion,
          stdexec::transform_completion_signatures_of<
            stdexec::__child_of<Self>,
            Env,
            stdexec::completion_signatures<stdexec::set_stopped_t()>,
            stdexec::__mconst<stdexec::completion_signatures<>>::__f>>;


      template <stdexec::sender_expr_for<repeat_t> Self, class Env>
      static auto get_item_types(Self&&, Env&&) noexcept
        -> exec::item_types_of_t<stdexec::__child_of<Self>, Env>;

      template <stdexec::sender_expr_for<repeat_t> Self, class Receiver>
        requires exec::sequence_sender_to<
                   stdexec::__child_of<Self>,
                   receiver<stdexec::__child_of<Self>, Receiver >>
      static auto subscribe(Self&& self, Receiver rcvr) noexcept(
        stdexec::__nothrow_invocable<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>>)
        -> stdexec::__call_result_t<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>> {
        return stdexec::__sexpr_apply(static_cast<Self&&>(self), subscribe_fn<Receiver>{rcvr});
      }
    };
  }

  inline constexpr repeat_::repeat_t repeat{};
}
