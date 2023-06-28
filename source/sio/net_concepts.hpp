/*
 * Copyright (c) 2023 Maikel Nadolski
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "./async_allocator.hpp"
#include "./async_resource.hpp"
#include "./sequence/sequence_concepts.hpp"
#include "./concepts.hpp"

namespace sio {
  template <class IP>
  concept internet_protocol = requires(const IP& proto) {
    { proto.family() };
    { proto.type() };
    { proto.protocol() };
  };
}

namespace sio::async {
  namespace socket_ {
    struct socket_t;
  };

  using socket_::socket_t;
  extern const socket_t socket;

  namespace socket_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).socket(socket, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).socket(socket, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::socket(static_cast<Tp&&>(t), socket, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::socket(static_cast<Tp&&>(t), socket, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct socket_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).socket(socket_t{}, static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::socket(
            static_cast<Tp&&>(t), socket_t{}, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const socket_t socket{};

  namespace connect_ {
    struct connect_t;
  };

  using connect_::connect_t;
  extern const connect_t connect;

  namespace connect_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).connect(connect, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).connect(connect, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::connect(static_cast<Tp&&>(t), connect, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      {
        decay_t<Tp>::connect(static_cast<Tp&&>(t), connect, static_cast<Args&&>(args)...)
      } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct connect_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).connect(connect_t{}, static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::connect(
            static_cast<Tp&&>(t), connect_t{}, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const connect_t connect{};

  namespace accept_once_ {
    struct accept_once_t;
  };

  using accept_once_::accept_once_t;
  extern const accept_once_t accept_once;

  namespace accept_once_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).accept_once(accept_once, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).accept_once(accept_once, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::accept_once(static_cast<Tp&&>(t), accept_once, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      {
        decay_t<Tp>::accept_once(static_cast<Tp&&>(t), accept_once, static_cast<Args&&>(args)...)
      } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    struct accept_once_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).accept_once(accept_once_t{}, static_cast<Args&&>(args)...);
        } else {
          return decay_t<Tp>::accept_once(
            static_cast<Tp&&>(t), accept_once_t{}, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  inline const accept_once_t accept_once{};

  namespace accept_ {
    struct accept_t;
  };

  using accept_::accept_t;
  extern const accept_t accept;

  namespace accept_ {
    template <class Tp, class... Args>
    concept has_member_cpo = requires(Tp&& t, Args&&... args) {
      static_cast<Tp&&>(t).accept(accept, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_member_cpo = requires(Tp&& t, Args&&... args) {
      { static_cast<Tp&&>(t).accept(accept, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      decay_t<Tp>::accept(static_cast<Tp&&>(t), accept, static_cast<Args&&>(args)...);
    };

    template <class Tp, class... Args>
    concept nothrow_has_static_member_cpo = requires(Tp&& t, Args&&... args) {
      { decay_t<Tp>::accept(static_cast<Tp&&>(t), accept, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Tp, class... Args>
    concept has_customization = has_member_cpo<Tp, Args...> || has_static_member_cpo<Tp, Args...>;

    template <class Tp, class... Args>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp, Args...> || nothrow_has_static_member_cpo<Tp, Args...>;

    template <class Acceptor, class Receiver>
    struct operation_base;

    template <class Acceptor, class Receiver>
    union client_operation;

    struct on_stop_requested {
      stdexec::in_place_stop_source& stop_source_;

      void operator()() const noexcept {
        stop_source_.request_stop();
      }
    };

    template <class Rcvr>
    using env_t = exec::make_env_t<
      stdexec::env_of_t<Rcvr>,
      exec::with_t<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>;

    template <class Acceptor, class Receiver>
    struct next_receiver {
      using is_receiver = void;

      operation_base<Acceptor, Receiver>* op_;
      client_operation<Acceptor, Receiver>* client_op_;

      void complete() noexcept {
        operation_base<Acceptor, Receiver>* op = op_;
        op->destroy(client_op_);
        op->deallocate(client_op_);
      }

      void set_value(stdexec::set_value_t) && noexcept {
        complete();
      }

      void set_error(stdexec::set_error_t, std::error_code error) && noexcept {
        op_->notify_error(error);
        complete();
      }

      void set_error(stdexec::set_error_t, std::exception_ptr error) && noexcept {
        op_->notify_error(static_cast<std::exception_ptr&&>(error));
        complete();
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->notify_stopped();
        complete();
      }

      env_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return exec::make_env(
          stdexec::get_env(op_->rcvr_),
          exec::with(stdexec::get_stop_token, op_->stop_source_.get_token()));
      }
    };

    template <class Acceptor, class Receiver>
    struct operation_next {
      Receiver rcvr_;
      Acceptor acceptor_;

      operation_next(Receiver rcvr, Acceptor acceptor)
        : rcvr_(static_cast<Receiver&&>(rcvr))
        , acceptor_(acceptor) {
      }

      virtual void start_next() noexcept = 0;
    };

    template <class Acceptor, class Receiver>
    auto next_accept_sender(operation_next<Acceptor, Receiver>& op) {
      return stdexec::let_value(async::accept_once(op.acceptor_), [&op](auto fd) {
        return exec::finally(
          exec::set_next(
            op.rcvr_,
            stdexec::then(
              stdexec::just(fd),
              [&op](auto client) noexcept {
                op.start_next();
                return client;
              })),
          async::close(fd));
      });
    }

    template <class Acceptor, class Receiver>
    struct delete_receiver {
      using is_receiver = void;
      operation_base<Acceptor, Receiver>* op_;

      stdexec::empty_env get_env(stdexec::get_env_t) const noexcept {
        return {};
      }

      void set_value(stdexec::set_value_t) && noexcept {
        op_->decrease_ref();
      }

      template <class E>
      void set_error(stdexec::set_error_t, E&& error) && noexcept {
        op_->notify_error(static_cast<E&&>(error));
        op_->decrease_ref();
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->notify_stopped();
        op_->decrease_ref();
      }
    };

    template <class Acceptor, class Receiver>
    using next_accept_sender_of_t =
      decltype(next_accept_sender(*(operation_next<Acceptor, Receiver>*) nullptr));

    template <class Receiver>
    using allocator_of_t =
      decltype(async::get_allocator(stdexec::get_env(std::declval<Receiver>())));

    template <class Acceptor, class Receiver>
    using allocator_t = typename std::allocator_traits<
      allocator_of_t<Receiver>>::template rebind_alloc<client_operation<Acceptor, Receiver>>;

    template <class Acceptor, class Receiver>
    using deallocate_sender_t = decltype(async::destroy_and_deallocate(
      std::declval<allocator_t<Acceptor, Receiver>>(),
      (client_operation<Acceptor, Receiver>*) nullptr));

    template <class Acceptor, class Receiver>
    union client_operation {
      ~client_operation() {
        std::destroy_at(&delete_op_);
      }

      stdexec::connect_result_t<
        next_accept_sender_of_t<Acceptor, Receiver>,
        next_receiver<Acceptor, Receiver>>
        next_op_;
      stdexec::connect_result_t<
        deallocate_sender_t<Acceptor, Receiver>,
        delete_receiver<Acceptor, Receiver>>
        delete_op_;
    };

    template <class Acceptor, class Receiver>
    struct operation_base : operation_next<Acceptor, Receiver> {

      std::atomic<std::size_t> ref_count_{};
      std::atomic<int> error_emplaced_{0};
      std::variant<std::monostate, std::exception_ptr, std::error_code> error_{};
      stdexec::in_place_stop_source stop_source_;
      using callback_type = typename stdexec::stop_token_of_t<
        stdexec::env_of_t<Receiver>>::template callback_type<on_stop_requested>;
      std::optional<callback_type> stop_callback_;

      operation_base(Receiver rcvr, Acceptor acceptor) noexcept
        : operation_next<Acceptor, Receiver>{static_cast<Receiver&&>(rcvr), acceptor} {
      }

      void increase_ref() noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
      }

      void decrease_ref() noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_relaxed) == 1) {
          this->stop_callback_.reset();
          if (error_emplaced_.load(std::memory_order_acquire) == 2) {
            std::visit(
              [this]<class Error>(Error&& error) noexcept {
                if constexpr (stdexec::__not_decays_to<Error, std::monostate>) {
                  stdexec::set_error(
                    static_cast<Receiver&&>(this->rcvr_), static_cast<Error&&>(error));
                }
              },
              std::move(error_));
          }
          exec::set_value_unless_stopped(static_cast<Receiver&&>(this->rcvr_));
        }
      }

      allocator_t<Acceptor, Receiver> get_allocator() const noexcept {
        return allocator_t<Acceptor, Receiver>(
          sio::async::get_allocator(stdexec::get_env(this->rcvr_)));
      }

      void deallocate(client_operation<Acceptor, Receiver>* op) noexcept {
        SIO_ASSERT(op != nullptr);
        std::construct_at(&op->delete_op_, stdexec::__conv{[&] {
            return stdexec::connect(
            sio::async::destroy_and_deallocate(get_allocator(), op),
            delete_receiver<Acceptor, Receiver>{this});
        }});
        stdexec::start(op->delete_op_);
      }

      template <class Error>
      void notify_error(Error error) noexcept {
        int expected = 0;
        if (error_emplaced_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          error_ = error;
          error_emplaced_.store(2, std::memory_order_release);
        }
        stop_source_.request_stop();
      }

      void notify_stopped() noexcept {
        stop_source_.request_stop();
      }

      void destroy(void* op) {
        std::destroy_at(&static_cast<client_operation<Acceptor, Receiver>*>(op)->next_op_);
      }
    };

    template <class Acceptor, class Receiver>
    struct allocation_receiver {
      operation_base<Acceptor, Receiver>* op_;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }

      void set_value(stdexec::set_value_t, client_operation<Acceptor, Receiver>* client_op) && noexcept {
        try {
          if (op_->stop_source_.stop_requested()) {
            op_->deallocate(client_op);
            return;
          }
          std::construct_at(&client_op->next_op_, stdexec::__conv{[&] {
            return stdexec::connect(
              next_accept_sender(*op_), next_receiver<Acceptor, Receiver>{op_, client_op});
          }});
          stdexec::start(client_op->next_op_);
        } catch (...) {
          op_->notify_error(std::current_exception());
          op_->deallocate(client_op);
        }
      }

      template <class Error>
      void set_error(stdexec::set_error_t, Error&& err) && noexcept {
        op_->notify_error(static_cast<Error&&>(err));
        op_->decrease_ref();
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        op_->notify_stopped();
        op_->decrease_ref();
      }
    };

    template <class Acceptor, class Receiver>
    struct operation : operation_base<Acceptor, Receiver> {
      using next_allocator_t =
        decltype(std::declval<const operation_base<Acceptor, Receiver>&>().get_allocator());
      using allocate_t = decltype(sio::async::allocate(std::declval<next_allocator_t>(), 0));

      operation(Receiver rcvr, Acceptor acceptor)
        : operation_base<Acceptor, Receiver>{static_cast<Receiver&&>(rcvr), acceptor} {
      }

      std::optional<stdexec::connect_result_t<allocate_t, allocation_receiver<Acceptor, Receiver>>>
        op_{};

      void start_next() noexcept override {
        this->increase_ref();
        try {
          stdexec::start(op_.emplace(stdexec::__conv{[&] {
            auto alloc = this->get_allocator();
            return stdexec::connect(
              sio::async::allocate(alloc, 1), allocation_receiver<Acceptor, Receiver>{this});
          }}));
        } catch (...) {
          this->notify_error(std::current_exception());
          this->decrease_ref();
        }
      }

      void start(stdexec::start_t) noexcept {
        this->stop_callback_.emplace(
          stdexec::get_stop_token(stdexec::get_env(this->rcvr_)),
          on_stop_requested{this->stop_source_});
        start_next();
      }
    };

    template <class Acceptor>
    struct sequence {
      using is_sender = exec::sequence_tag;

      Acceptor acceptor_;

      explicit sequence(Acceptor a) noexcept
        : acceptor_{a} {
      }

      using accept_once_sender = decltype(accept_once(std::declval<Acceptor>()));

      template < decays_to<sequence> Self, class Receiver>
      static operation<Acceptor, Receiver>
        subscribe(Self&& self, exec::subscribe_t, Receiver rcvr) //
        noexcept(nothrow_decay_copyable<Receiver>) {
        return {static_cast<Receiver&&>(rcvr), self.acceptor_};
      }

      template <decays_to<sequence> Self, class Env>
      static auto get_completion_signatures(Self&&, stdexec::get_completion_signatures_t, Env&&)
        -> stdexec::__msuccess_or_t<stdexec::__try_make_completion_signatures<
          accept_once_sender,
          Env,
          stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>>>;
    };

    struct accept_t {
      template <class Tp, class... Args>
        requires has_customization<Tp, Args...> || constructible_from<sequence<decay_t<Tp>>, Tp>
      auto operator()(Tp&& t, Args&&... args) const
        noexcept(nothrow_has_customization<Tp, Args...>) {
        if constexpr (has_member_cpo<Tp, Args...>) {
          return static_cast<Tp&&>(t).accept(accept_t{}, static_cast<Args&&>(args)...);
        } else if constexpr (has_static_member_cpo<Tp, Args...>) {
          return decay_t<Tp>::accept(
            static_cast<Tp&&>(t), accept_t{}, static_cast<Args&&>(args)...);
        } else {
          return sequence<decay_t<Tp>>{static_cast<Tp&&>(t)};
        }
      }
    };
  }

  inline const accept_t accept{};
}