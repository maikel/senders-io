#pragma once

#include <exception>
#include <stdexec/__detail/__completion_signatures.hpp>
#include <stdexec/__detail/__execution_fwd.hpp>
#include <stdexec/__detail/__sender_introspection.hpp>
#include <stdexec/execution.hpp>
#include <exec/sequence_senders.hpp>
#include "sio/sequence/sequence_concepts.hpp"
#include <utility>

#include "test_helpers.hpp"

namespace iterate_1_to_5_ {
  using item_sender = decltype(stdexec::just(5));

  template <class Receiver>
  struct iterate_1_to_5_op;

  template <class Receiver>
  struct next_receiver {
    using receiver_concept = stdexec::receiver_t;
    iterate_1_to_5_op<Receiver>* op = nullptr;

    void set_value() noexcept {
      op->start_next();
    }

    void set_stopped() noexcept {
      exec::set_value_unless_stopped(std::move(op->rcvr));
    }
  };

  template <class Receiver>
  struct iterate_1_to_5_op {
    using next_sender_t = exec::next_sender_of_t< Receiver, item_sender>;
    using op_t = stdexec::connect_result_t<next_sender_t, next_receiver<Receiver>>;
    std::optional<op_t> op{};

    Receiver rcvr;
    int cnt;

    iterate_1_to_5_op(Receiver r)
      : rcvr(std::move(r))
      , cnt{0} {
    }

    auto start_next() noexcept {
      if (this->cnt == 5) {
        stdexec::set_value(std::move(this->rcvr));
      } else
        try {
          ++this->cnt;
          stdexec::start(op.emplace(stdexec::__emplace_from{[this] {
            auto next_sender = exec::set_next(this->rcvr, stdexec::just(cnt));
            return stdexec::connect(std::move(next_sender), next_receiver{this});
          }}));
        } catch (...) {
          stdexec::set_error(std::move(this->rcvr), std::current_exception());
        }
    }

    void start() noexcept {
      start_next();
    }
  };

  struct iterate_1_to_5 {
    using sender_concept = exec::sequence_sender_t;
    using item_types = exec::item_types<item_sender>;

    auto get_env() const noexcept -> stdexec::empty_env {
      return {};
    }

    template <class... Env>
    auto get_completion_signatures(Env&&...) noexcept
      -> stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(eptr),
        stdexec::set_stopped_t()> {
      return {};
    }

    template <class R>
    friend auto
      tag_invoke(exec::subscribe_t, iterate_1_to_5&& self, R r) noexcept -> iterate_1_to_5_op<R> {
      return {std::move(r)};
    }
  };

}

// namespace iterate_1_to_5_cust_ {
//
//   struct early_domain {
//     template <class Sender>
//       requires stdexec::sender_expr_for< Sender, sio::first_t>
//     auto transform_sender(Sender&& sndr) noexcept {
//       ++n;
//       return sndr;
//     }
//
//     static int n;
//   };
//
//   inline int early_domain::n = 0;
//
//   struct iter_env {
//     auto query(stdexec::get_domain_t) const noexcept -> early_domain {
//       return {};
//     }
//   };
//
//   struct iterate_1_to_5 {
//     using sender_concept = exec::sequence_sender_t;
//     using item_types = exec::item_types<iterate_1_to_5_::item_sender>;
//
//     auto get_env() const noexcept -> iter_env {
//       return {};
//     }
//
//     template <class Env>
//     auto get_completion_signatures(Env&&...) noexcept
//       -> stdexec::completion_signatures<
//         stdexec::set_value_t(),
//         stdexec::set_error_t(eptr),
//         stdexec::set_stopped_t()> {
//       return {};
//     }
//
//     template <class R>
//     friend auto tag_invoke(exec::subscribe_t, iterate_1_to_5&& self, R r) noexcept
//       -> iterate_1_to_5_::iterate_1_to_5_op<R> {
//       return {std::move(r)};
//     }
//   };
//
//
// }

using iterate_1_to_5_::iterate_1_to_5;
