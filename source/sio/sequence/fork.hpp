#pragma once

#include "../concepts.hpp"
#include "../async_allocator.hpp"
#include "./sequence_concepts.hpp"

#include <exec/env.hpp>
#include <exec/variant_sender.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/__detail/__basic_sequence.hpp>
#include <stdexec/__detail/__concepts.hpp>

namespace sio {
  namespace fork_ {

    template <class ThenSender, class ElseSender>
    exec::variant_sender<ThenSender, ElseSender>
      if_then_else(bool condition, ThenSender then, ElseSender otherwise) {
      if (condition) {
        return then;
      }
      return otherwise;
    }

    template <class SeqRcvr, class ErrorsVariant>
    struct operation_base;

    template <class SeqRcvr, class ErrorsVariant>
    struct on_stop_requested {
      operation_base<SeqRcvr, ErrorsVariant>* op_;

      void operator()() const noexcept;
    };

    template <class SeqRcvr, class ErrorsVariant>
    struct operation_base {
      enum completion_type {
        value = 0,
        error = 2,
        stopped = 3
      };

      explicit operation_base(SeqRcvr rcvr) noexcept
        : next_rcvr_{static_cast<SeqRcvr&&>(rcvr)} {
      }

      SeqRcvr next_rcvr_;
      std::atomic<std::ptrdiff_t> ref_counter_{0};
      std::atomic<bool> is_stop_requested_{0};
      std::atomic<int> completion_type_{0};
      ErrorsVariant error_{};
      stdexec::inplace_stop_source stop_source_{};
      using stop_token_t = stdexec::stop_token_of_t<stdexec::env_of_t<SeqRcvr>>;
      using callback_t =
        typename stop_token_t::template callback_type<on_stop_requested<SeqRcvr, ErrorsVariant>>;
      std::optional< callback_t > stop_callback_{};

      template <class Tp>
      auto get_allocator() {
        auto base = sio::async::get_allocator(stdexec::get_env(next_rcvr_));
        typename std::allocator_traits<decltype(base)>::template rebind_alloc<Tp> alloc{base};
        return alloc;
      }

      bool increase_ref() {
        std::ptrdiff_t expected = 1;
        while (
          !ref_counter_.compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed)) {
          if (expected == 0) {
            return false;
          }
        }
        return true;
      }

      template <class Error>
      void set_error(Error err) {
        int expected = completion_type::value;
        if (completion_type_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          error_.template emplace<Error>(static_cast<Error&&>(err));
          expected = 1;
          completion_type_.compare_exchange_strong(
            expected, completion_type::error, std::memory_order_release);
        }
      }

      void set_stopped() {
        completion_type_.store(completion_type::stopped, std::memory_order_relaxed);
      }

      struct VisitError {
        SeqRcvr rcvr_;

        template <class Error>
        void operator()(Error err) noexcept {
          if constexpr (decays_to<Error, std::monostate>) {

          } else {
            stdexec::set_error(static_cast<SeqRcvr&&>(rcvr_), static_cast<Error&&>(err));
          }
        }
      };

      void complete() {
        stop_callback_.reset();
        int ctype = completion_type_.load(std::memory_order_acquire);
        if (ctype == completion_type::stopped) {
          stdexec::set_stopped(static_cast<SeqRcvr&&>(next_rcvr_));
        } else if (ctype == completion_type::error) {
          std::visit(
            VisitError{static_cast<SeqRcvr&&>(next_rcvr_)}, static_cast<ErrorsVariant&&>(error_));
        } else {
          exec::set_value_unless_stopped(static_cast<SeqRcvr&&>(next_rcvr_));
        }
      }

      void decrease_ref() {
        if (ref_counter_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          complete();
        }
      }

      void request_stop() {
        if (!is_stop_requested_.exchange(true, std::memory_order_relaxed)) {
          stop_source_.request_stop();
        }
      }

      auto stop_requested() const noexcept -> bool {
        return is_stop_requested_.load();
      }
    };

    template <class SeqRcvr, class ErrorsVariant>
    void on_stop_requested<SeqRcvr, ErrorsVariant>::operator()() const noexcept {
      op_->request_stop();
      op_->decrease_ref();
    }

    template <class Item, class SeqRcvr, class ErrorsVariant>
    struct item_operation;

    template <class Env>
    using allocator_of_t = decltype(sio::async::get_allocator(std::declval<const Env&>()));

    template <class Rcvr, class T>
    using rcvr_allocator_t = typename std::allocator_traits<
      allocator_of_t<stdexec::env_of_t<Rcvr>>>::template rebind_alloc<T>;

    template <class Allocator, class... Args>
    using async_new_sender_of_t =
      decltype(sio::async::async_new(std::declval<Allocator>(), std::declval<Args>()...));

    template <class Item, class SeqRcvr, class ErrorsVariant>
    using async_new_sender = async_new_sender_of_t<
      rcvr_allocator_t<SeqRcvr, item_operation<Item, SeqRcvr, ErrorsVariant>>,
      Item,
      operation_base<SeqRcvr, ErrorsVariant>*>;

    template <class Allocator, class Tp>
    using async_delete_sender_of_t =
      decltype(sio::async::async_delete(std::declval<Allocator>(), std::declval<Tp>()));

    template <class Item, class SeqRcvr, class ErrorsVariant>
    using async_delete_sender = async_delete_sender_of_t<
      rcvr_allocator_t<SeqRcvr, item_operation<Item, SeqRcvr, ErrorsVariant>>,
      item_operation<Item, SeqRcvr, ErrorsVariant>*>;


    template <class SeqRcvr>
    using env_t = exec::make_env_t<
      stdexec::env_of_t<SeqRcvr>,
      exec::with_t<stdexec::get_stop_token_t, stdexec::inplace_stop_token>>;

    template <class Item, class SeqRcvr, class ErrorsVariant>
    struct item_receiver {
      using receiver_concept = stdexec::receiver_t;

      item_operation<Item, SeqRcvr, ErrorsVariant>* item_op_;

      void set_value() noexcept;

      void set_stopped() noexcept;

      auto get_env() const noexcept -> env_t<SeqRcvr>;
    };

    template <class SeqRcvr, class ErrorsVariant>
    struct final_receiver {
      using receiver_concept = stdexec::receiver_t;

      operation_base<SeqRcvr, ErrorsVariant>* sequence_op_;

      void set_value() noexcept {
        sequence_op_->decrease_ref();
      }

      auto get_env() const noexcept -> env_t<SeqRcvr> {
        return exec::make_env(
          stdexec::get_env(sequence_op_->next_rcvr_),
          exec::with(stdexec::get_stop_token, sequence_op_->stop_source_.get_token()));
      }
    };

    template <class Item, class SeqRcvr, class ErrorsVariant>
    struct item_operation {
      explicit item_operation(Item item, operation_base<SeqRcvr, ErrorsVariant>* base)
        : sequence_op_{base}
        , inner_operations_{
            exec::set_next(base->next_rcvr_, static_cast<Item&&>(item)),
            item_receiver_t{this}} {
      }

      using next_sender_t = exec::next_sender_of_t<SeqRcvr, Item>;
      using item_receiver_t = item_receiver<Item, SeqRcvr, ErrorsVariant>;

      using async_delete_t = async_delete_sender<Item, SeqRcvr, ErrorsVariant>;
      using final_receiver_t = final_receiver<SeqRcvr, ErrorsVariant>;

      union inner_operations_t {
        explicit inner_operations_t(next_sender_t&& next, item_receiver_t rcvr)
          : next_(stdexec::connect(
              static_cast<next_sender_t&&>(next),
              static_cast<item_receiver_t&&>(rcvr))) {
        }

        ~inner_operations_t() {
          std::destroy_at(&async_delete_);
        }

        stdexec::connect_result_t<next_sender_t, item_receiver_t> next_;
        stdexec::connect_result_t<async_delete_t, final_receiver_t> async_delete_;
      };

      operation_base<SeqRcvr, ErrorsVariant>* sequence_op_;
      inner_operations_t inner_operations_;

      void start_delete_operation() noexcept {
        // destroy next_ operation
        std::destroy_at(&inner_operations_.next_);

        // start async_delete options
        auto alloc = sequence_op_->template get_allocator<item_operation>();

        std::construct_at(&inner_operations_.async_delete_, stdexec::__conv{[&] {
          return stdexec::connect(
            sio::async::async_delete(alloc, this), final_receiver_t{sequence_op_});
        }});
        stdexec::start(inner_operations_.async_delete_);
      }

      void start() noexcept {
        if (sequence_op_->stop_source_.stop_requested()) {
          start_delete_operation();
        } else {
          stdexec::start(inner_operations_.next_);
        }
      }
    };

    template <class Item, class SeqRcvr, class ErrorsVariant>
    void item_receiver<Item, SeqRcvr, ErrorsVariant>::set_value() noexcept {
      item_op_->start_delete_operation();
    }

    template <class Item, class SeqRcvr, class ErrorsVariant>
    void item_receiver<Item, SeqRcvr, ErrorsVariant>::set_stopped() noexcept {
      item_op_->sequence_op_->request_stop();
      item_op_->start_delete_operation();
    }

    template <class Item, class SeqRcvr, class ErrorsVariant>
    auto item_receiver<Item, SeqRcvr, ErrorsVariant>::get_env() const noexcept -> env_t<SeqRcvr> {
      return exec::make_env(
        stdexec::get_env(item_op_->sequence_op_->next_rcvr_),
        exec::with(stdexec::get_stop_token, item_op_->sequence_op_->stop_source_.get_token()));
    }

    template <class SeqRcvr, class ErrorsVariant>
    struct receiver {
      using receiver_concept = stdexec::receiver_t;

      operation_base<SeqRcvr, ErrorsVariant>* op_;

      template <class Item>
      friend auto tag_invoke(exec::set_next_t, receiver& self, Item&& item) {
        return stdexec::just(static_cast<Item&&>(item))
             | stdexec::let_value([op = self.op_](Item& item) noexcept {
                 return if_then_else(
                   op->increase_ref(), static_cast<Item&&>(item), stdexec::just_stopped());
               })
             | stdexec::let_value([op = self.op_]<class... Vals>(Vals&&... values) noexcept {
                 using just_t = decltype(stdexec::just(std::forward<Vals>(values)...));
                 using item_op_t = item_operation<just_t, SeqRcvr, ErrorsVariant>;
                 return sio::async::async_new(
                          op->template get_allocator< item_op_t>(),
                          stdexec::just(std::forward<Vals>(values)...),
                          op)
                      | stdexec::then([](item_op_t* item_op) noexcept {
                          stdexec::start(*item_op);
                        });
               })
             | stdexec::upon_stopped([op = self.op_]() noexcept {
                 op->request_stop();
                 op->decrease_ref();
               })
             | stdexec::upon_error([op = self.op_]<class Err>(Err&& err) noexcept {
                 op->set_error(static_cast<Err&&>(err));
                 op->request_stop();
                 op->decrease_ref();
               });
      }

      void set_value() && noexcept {
        op_->decrease_ref();
      }

      template <class Error>
      void set_error(Error err) && noexcept {
        op_->set_error(static_cast<Error&&>(err));
        op_->request_stop();
        op_->decrease_ref();
      }

      void set_stopped() && noexcept {
        op_->set_stopped();
        op_->request_stop();
        op_->decrease_ref();
      }

      auto get_env() const noexcept {
        return exec::make_env(
          stdexec::get_env(op_->next_rcvr_),
          exec::with(stdexec::get_stop_token, op_->stop_source_.get_token()));
      }
    };

    template <class Sequence, class Env>
    struct traits {
      template <class... Ts>
      using just_item_t = decltype(stdexec::just(std::declval<Ts>()...));

      using item_types = stdexec::__gather_completions<
        stdexec::set_value_t,
        exec::item_completion_signatures_of_t<Sequence, Env>,
        stdexec::__q<just_item_t>,
        stdexec::__q<exec::item_types>>;

      using errors_variant = stdexec::__minvoke<
        stdexec::__mconcat<stdexec::__qq<stdexec::__nullable_std_variant>>,
        stdexec::error_types_of_t<Sequence, Env, stdexec::__types>,
        stdexec::__types<std::exception_ptr>>;

      template <class... Es>
      using to_error_sig = stdexec::completion_signatures<stdexec::set_error_t(decay_t<Es>)...>;

      using compl_sigs = stdexec::__concat_completion_signatures<
        stdexec::completion_signatures_of_t<Sequence, Env>,
        stdexec::error_types_of_t<Sequence, Env, to_error_sig>,
        stdexec::completion_signatures<stdexec::set_stopped_t()>>;
    };


    template <class Sequence, class SeqRcvr>
    using base_type = operation_base<
      SeqRcvr,
      typename traits<Sequence, stdexec::env_of_t<SeqRcvr>>::errors_variant>;

    template <class Sequence, class SeqRcvr>
    struct operation : base_type<Sequence, SeqRcvr> {
      using env = stdexec::env_of_t<SeqRcvr>;
      using errors_variant = typename traits<Sequence, env>::errors_variant;
      using receiver_t = receiver<SeqRcvr, errors_variant>;
      using subscribe_result_t = exec::subscribe_result_t<Sequence, receiver_t>;

      subscribe_result_t op_;

      operation(Sequence&& seq, SeqRcvr rcvr)
        : base_type<Sequence, SeqRcvr>(static_cast<SeqRcvr&&>(rcvr))
        , op_{exec::subscribe(static_cast<Sequence&&>(seq), receiver_t{this})} {
      }

      void start() noexcept {
        this->stop_callback_.emplace(
          stdexec::get_stop_token(stdexec::get_env(this->next_rcvr_)),
          on_stop_requested<SeqRcvr, errors_variant>{this});
        if (this->stop_source_.stop_requested()) {
          this->stop_callback_.reset();
          stdexec::set_stopped(static_cast<SeqRcvr&&>(this->next_rcvr_));
        } else {
          this->ref_counter_.store(1, std::memory_order_relaxed);
          stdexec::start(op_);
        }
      }
    };

    template <class SeqRcvr>
    struct subscribe_fn {
      SeqRcvr& rcvr_;

      template <class Child>
      using receiver_t = receiver< SeqRcvr, typename traits<Child, SeqRcvr>::errors_variant>;

      template <class Sequence>
        requires exec::sequence_sender_to< Sequence, receiver_t<Sequence> >
      auto operator()(stdexec::__ignore, stdexec::__ignore, Sequence&& sequence) //
        -> operation<Sequence, SeqRcvr> {
        return {static_cast<Sequence&&>(sequence), static_cast<SeqRcvr&&>(rcvr_)};
      }
    };

    struct fork_t {
      template <stdexec::sender Sender>
      auto operator()(Sender&& sndr) const noexcept -> stdexec::__well_formed_sender auto {
        auto domain = stdexec::__get_early_domain(sndr);
        return stdexec::transform_sender(
          domain, exec::make_sequence_expr<fork_t>(stdexec::__{}, static_cast<Sender&&>(sndr)));
      }

      auto operator()() const noexcept -> binder_back<fork_t> {
        return {{}, {}, {}};
      }

      template <stdexec::sender_expr_for<fork_t> Self, class Env>
      static auto get_completion_signatures(Self&&, Env&&) noexcept ->
        typename traits<stdexec::__child_of<Self>, Env>::compl_sigs {
        return {};
      }

      template <stdexec::sender_expr_for<fork_t> Self, class Env>
      static auto get_item_types(Self&&, Env&&) noexcept ->
        typename traits<stdexec::__child_of<Self>, Env>::item_types {
        return {};
      }

      template <stdexec::sender_expr_for<fork_t> Self, stdexec::receiver Receiver>
      static auto subscribe(Self&& self, Receiver rcvr) noexcept(
        stdexec::__nothrow_callable<
          stdexec::__call_result_t<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>>>)
        -> stdexec::__call_result_t<stdexec::__sexpr_apply_t, Self, subscribe_fn<Receiver>> {
        return stdexec::__sexpr_apply(static_cast<Self&&>(self), subscribe_fn<Receiver>{rcvr});
      }
    };

  }

  using fork_::fork_t;
  inline constexpr fork_::fork_t fork{};
}
