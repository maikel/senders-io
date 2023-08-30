#pragma once

#include "../async_allocator.hpp"
#include "./sequence_concepts.hpp"

#include <exec/variant_sender.hpp>
#include <exec/env.hpp>

#include <memory>

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
        : next_rcvr_{std::move(rcvr)} {
      }

      SeqRcvr next_rcvr_;
      std::atomic<std::ptrdiff_t> ref_counter_{0};
      std::atomic<bool> is_stop_requested_{0};
      std::atomic<int> completion_type_{0};
      ErrorsVariant error_{};
      stdexec::in_place_stop_source stop_source_{};
      using stop_token_t = stdexec::stop_token_of_t<stdexec::env_of_t<SeqRcvr>>;
      std::optional<
        typename stop_token_t::template callback_type<on_stop_requested<SeqRcvr, ErrorsVariant>>>
        stop_callback_{};

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
          error_.template emplace<Error>(std::move(err));
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
        void operator()(Error&& err) noexcept {
          if constexpr (decays_to<Error, std::monostate>) {

          } else {
            stdexec::set_error(std::move(rcvr_), std::forward<Error>(err));
          }
        }
      };

      void complete() {
        stop_callback_.reset();
        int ctype = completion_type_.load(std::memory_order_acquire);
        if (ctype == completion_type::stopped) {
          stdexec::set_stopped(std::move(next_rcvr_));
        } else if (ctype == completion_type::error) {
          std::visit(VisitError{std::move(next_rcvr_)}, std::move(error_));
        } else {
          exec::set_value_unless_stopped(std::move(next_rcvr_));
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
          decrease_ref();
        }
      }
    };

    template <class SeqRcvr, class ErrorsVariant>
    void on_stop_requested<SeqRcvr, ErrorsVariant>::operator()() const noexcept {
      op_->request_stop();
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

    template <class Item, class SeqRcvr, class ErrorsVariant>
    struct item_operation;

    template <class Item, class SeqRcvr, class ErrorsVariant>
    struct item_receiver {
      using is_receiver = void;

      item_operation<Item, SeqRcvr, ErrorsVariant>* item_op_;

      exec::make_env_t<
        stdexec::env_of_t<SeqRcvr>,
        exec::with_t<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>
        get_env(stdexec::get_env_t) const noexcept;

      void set_value(stdexec::set_value_t) && noexcept;

      void set_stopped(stdexec::set_stopped_t) && noexcept;
    };

    template <class SeqRcvr, class ErrorsVariant>
    struct final_receiver {
      using is_receiver = void;
      operation_base<SeqRcvr, ErrorsVariant>* sequence_op_;

      auto get_env(stdexec::get_env_t) const noexcept {
        auto env = stdexec::get_env(sequence_op_->next_rcvr_);
        return exec::make_env(
          std::move(env),
          exec::with(stdexec::get_stop_token, sequence_op_->stop_source_.get_token()));
      }

      void set_value(stdexec::set_value_t) && noexcept {
        sequence_op_->decrease_ref();
      }
    };

    template <class T>
    void destroy_at(T* p) {
      p->~T();
    }

    template <class Item, class SeqRcvr, class ErrorsVariant>
    struct item_operation {
      explicit item_operation(Item item, operation_base<SeqRcvr, ErrorsVariant>* base)
        : sequence_op_{base}
        , inner_operations_{
            exec::set_next(base->next_rcvr_, std::move(item)),
            item_receiver_t{this}} {
      }

      using next_sender_t = exec::next_sender_of_t<SeqRcvr, Item>;
      using item_receiver_t = item_receiver<Item, SeqRcvr, ErrorsVariant>;

      using async_delete_t = async_delete_sender<Item, SeqRcvr, ErrorsVariant>;
      using final_receiver_t = final_receiver<SeqRcvr, ErrorsVariant>;

      union inner_operations_t {
        explicit inner_operations_t(next_sender_t&& next, item_receiver_t rcvr)
          : next_(stdexec::connect(std::move(next), rcvr)) {
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

      void start(stdexec::start_t) noexcept {
        if (sequence_op_->stop_source_.stop_requested()) {
          start_delete_operation();
        } else {
          stdexec::start(inner_operations_.next_);
        }
      }
    };

    template <class Item, class SeqRcvr, class ErrorsVariant>
    exec::make_env_t<
      stdexec::env_of_t<SeqRcvr>,
      exec::with_t<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>
      item_receiver<Item, SeqRcvr, ErrorsVariant>::get_env(stdexec::get_env_t) const noexcept {
      return exec::make_env(
        stdexec::get_env(item_op_->sequence_op_->next_rcvr_),
        exec::with(stdexec::get_stop_token, item_op_->sequence_op_->stop_source_.get_token()));
    }

    template <class Item, class SeqRcvr, class ErrorsVariant>
    void item_receiver<Item, SeqRcvr, ErrorsVariant>::set_value(stdexec::set_value_t) && noexcept {
      item_op_->start_delete_operation();
    }

    template <class Item, class SeqRcvr, class ErrorsVariant>
    void
      item_receiver<Item, SeqRcvr, ErrorsVariant>::set_stopped(stdexec::set_stopped_t) && noexcept {
      item_op_->sequence_op_->request_stop();
      item_op_->start_delete_operation();
    }

    template <class SeqRcvr, class ErrorsVariant>
    struct receiver {
      using is_receiver = void;

      operation_base<SeqRcvr, ErrorsVariant>* sequence_op_;

      template <class Item>
      auto set_next(exec::set_next_t, Item&& item) {
        return stdexec::just(std::forward<Item>(item))
             | stdexec::let_value([op = sequence_op_](std::decay_t<Item>& item) {
                 return if_then_else(op->increase_ref(), std::move(item), stdexec::just_stopped());
               })
             | stdexec::let_value([op = sequence_op_]<class... Vals>(Vals&&... values) noexcept {
                 using just_t = decltype(stdexec::just(std::forward<Vals>(values)...));
                 return sio::async::async_new(
                          op->template get_allocator<
                            item_operation<just_t, SeqRcvr, ErrorsVariant>>(),
                          stdexec::just(std::forward<Vals>(values)...),
                          op)
                      | stdexec::then(
                          [](item_operation<just_t, SeqRcvr, ErrorsVariant>* op) noexcept {
                            stdexec::start(*op);
                          })
                      | stdexec::upon_stopped([op]() noexcept {
                          op->request_stop();
                          op->decrease_ref();
                        });
               })
             | stdexec::upon_error([op = sequence_op_](auto eptr) noexcept {
                 op->set_error(std::move(eptr));
                 op->request_stop();
                 op->decrease_ref();
               });
      }

      void set_value(stdexec::set_value_t) && noexcept {
        sequence_op_->request_stop();
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error err) && noexcept {
        sequence_op_->set_error(std::move(err));
        sequence_op_->request_stop();
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        sequence_op_->set_stopped();
        sequence_op_->request_stop();
      }

      auto get_env(stdexec::get_env_t) const noexcept {
        auto env = stdexec::get_env(sequence_op_->next_rcvr_);
        return exec::make_env(
          std::move(env),
          exec::with(stdexec::get_stop_token, sequence_op_->stop_source_.get_token()));
      }
    };

    template <class... Args>
    using decay_args = stdexec::completion_signatures<stdexec::set_value_t(std::decay_t<Args>...)>;

    template <class Sequence, class Env>
    struct traits {
      using errors_variant = stdexec::__minvoke<
        stdexec::__mconcat<stdexec::__nullable_variant_t>,
        stdexec::error_types_of_t<Sequence, Env, stdexec::__types >,
        stdexec::__types<std::exception_ptr>>;

      using compl_sigs = stdexec::make_completion_signatures<
        Sequence,
        Env,
        stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>,
        decay_args>;
    };

    template <class Sequence, class SeqRcvr>
    struct operation
      : operation_base<
          SeqRcvr,
          typename traits<Sequence, stdexec::env_of_t<SeqRcvr>>::errors_variant> {
      using ErrorsVariant = typename traits<Sequence, stdexec::env_of_t<SeqRcvr>>::errors_variant;

      using base_type = operation_base<SeqRcvr, ErrorsVariant>;

      exec::subscribe_result_t<Sequence, receiver<SeqRcvr, ErrorsVariant>> op_;

      operation(Sequence&& seq, SeqRcvr rcvr)
        : base_type(std::move(rcvr))
        , op_{
            exec::subscribe(std::forward<Sequence>(seq), receiver<SeqRcvr, ErrorsVariant>{this})} {
      }

      void start(stdexec::start_t) noexcept {
        this->stop_callback_.emplace(
          stdexec::get_stop_token(stdexec::get_env(this->next_rcvr_)),
          on_stop_requested<SeqRcvr, ErrorsVariant>{this});
        if (this->stop_source_.stop_requested()) {
          this->stop_callback_.reset();
          stdexec::set_stopped(std::move(this->next_rcvr_));
        } else {
          this->ref_counter_.store(1, std::memory_order_relaxed);
          stdexec::start(op_);
        }
      }
    };

    template <class Sequence>
    struct sequence {
      using is_sender = exec::sequence_tag;

      Sequence sequence_;

      template <decays_to<sequence> Self, stdexec::receiver Rcvr>
        requires exec::sequence_sender_to<
          copy_cvref_t<Self, Sequence>,
          receiver<Rcvr, typename traits<Sequence, Rcvr>::errors_variant>>
      static operation<copy_cvref_t<Self, Sequence>, Rcvr>
        subscribe(Self&& self, exec::subscribe_t, Rcvr rcvr) {
        return {std::forward<Self>(self).sequence_, std::move(rcvr)};
      }

      template <decays_to<sequence> Self, class Env>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&) ->
        typename traits<copy_cvref_t<Self, Sequence>, Env>::compl_sigs;
    };
  }

  struct fork_t {
    template <class Sequence>
    fork_::sequence<Sequence> operator()(Sequence seq) const noexcept {
      return {std::move(seq)};
    }

    stdexec::__binder_back<fork_t> operator()() const noexcept {
      return {{}, {}, {}};
    }
  };

  inline constexpr fork_t fork{};
}