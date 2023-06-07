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

#include "./sequence/first.hpp"
#include "./sequence/let_value_each.hpp"
#include "./sequence/zip.hpp"

#include <exec/finally.hpp>

namespace sio {
  namespace async_resource_ {
    struct open_t;
    extern const open_t open;

    template <class Tp>
    concept has_open_member_cpo = requires(Tp&& obj) { static_cast<Tp&&>(obj).open(open); };

    template <class Tp>
    concept nothrow_open_member_cpo = requires(Tp&& obj) {
      { static_cast<Tp&&>(obj).open(open) } noexcept;
    };

    template <class Tp>
    concept has_open_static_member_cpo = requires(Tp&& obj) {
      Tp::open(static_cast<Tp&&>(obj), open);
    };

    template <class Tp>
    concept nothrow_open_static_member_cpo = requires(Tp&& obj) {
      { Tp::open(static_cast<Tp&&>(obj), open) } noexcept;
    };

    struct open_t {
      template <class Res>
        requires has_open_member_cpo<Res&> || has_open_static_member_cpo<Res&>
      auto operator()(Res& resource) const
        noexcept(nothrow_open_member_cpo<Res&> || nothrow_open_static_member_cpo<Res&>) {
        if constexpr (has_open_member_cpo<Res&>) {
          return resource.open(open);
        } else {
          return Res::open(resource, open);
        }
      }
    };

    struct close_t;
    extern const close_t close;

    template <class Tp>
    concept has_close_member_cpo = requires(Tp&& obj) { static_cast<Tp&&>(obj).close(close); };

    template <class Tp>
    concept nothrow_close_member_cpo = requires(Tp&& obj) {
      { static_cast<Tp&&>(obj).close(close) } noexcept;
    };

    template <class Tp>
    concept has_close_static_member_cpo = requires(Tp&& obj) {
      Tp::close(static_cast<Tp&&>(obj), close);
    };

    template <class Tp>
    concept nothrow_close_static_member_cpo = requires(Tp&& obj) {
      { Tp::close(static_cast<Tp&&>(obj), close) } noexcept;
    };

    struct close_t {
      template <class Token>
        requires has_close_member_cpo<Token&> || has_close_static_member_cpo<Token&>
      auto operator()(const Token& token) const noexcept(
        nothrow_close_member_cpo<const Token&> || nothrow_close_static_member_cpo<const Token&>) {
        if constexpr (has_close_member_cpo<const Token&>) {
          return token.close(close);
        } else {
          return Token::close(token, close);
        }
      }
    };

    inline constexpr open_t open{};
    inline constexpr close_t close{};
  }

  using async_resource_::open_t;
  using async_resource_::open;
  using async_resource_::close_t;
  using async_resource_::close;

  template <class Token>
  using close_sender_of_t = decltype(close(std::declval<Token>()));

  template <class Resource>
  using open_sender_of_t = decltype(open(std::declval<Resource&>()));

  template <class Resource, class Env>
  concept with_open = requires(Resource& resource) {
    { open(resource) } -> stdexec::__single_typed_sender<Env>;
  };

  template <class Resource, class Env>
    requires with_open<Resource, Env>
  using token_of_t = stdexec::__single_sender_value_t<call_result_t<open_t, Resource&>, Env>;

  template <class Sndr, class Env = stdexec::empty_env>
  concept sender_of_void =                              //
    stdexec::sender_of<Sndr, stdexec::set_value_t()> && //
    stdexec::__single_typed_sender<Sndr, Env>;

  template <class Resource, class Env = stdexec::empty_env>
  concept with_open_and_close =
    with_open<Resource, Env> && requires(Resource& resource, token_of_t<Resource, Env>& token) {
      { close(token) } -> sender_of_void;
    };

  namespace async_resource_ {
    template <class Receiver>
    struct operation_rcvr_base {
      explicit operation_rcvr_base(Receiver rcvr)
        : rcvr_(static_cast<Receiver&&>(rcvr)) {
      }

      operation_rcvr_base(const operation_rcvr_base&) = delete;
      operation_rcvr_base(operation_rcvr_base&&) = delete;
      operation_rcvr_base& operator=(const operation_rcvr_base&) = delete;
      operation_rcvr_base& operator=(operation_rcvr_base&&) = delete;

      [[no_unique_address]] Receiver rcvr_;
    };

    template <class Receiver>
    struct final_receiver {
      using is_receiver = void;
      operation_rcvr_base<Receiver>* op_;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }

      void set_value(stdexec::set_value_t) && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
      }

      template <class Err>
      void set_error(stdexec::set_error_t, Err&& err) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), static_cast<Err&&>(err));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->rcvr_));
      }
    };

    template <class Token, class ItemReceiver>
    struct run_operation {
      std::optional<Token>& token_;
      [[no_unique_address]] ItemReceiver rcvr_;

      void start(stdexec::start_t) noexcept {
        stdexec::set_value(static_cast<ItemReceiver&&>(rcvr_), static_cast<Token&&>(*token_));
      }
    };

    template <class Token>
    struct run_sender {
      using is_sender = void;

      std::optional<Token>& token_;

      using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t(Token&&)>;

      template <class ItemReceiver>
      auto connect(stdexec::connect_t, ItemReceiver rcvr) const
        noexcept(nothrow_decay_copyable<ItemReceiver>) -> run_operation<Token, ItemReceiver> {
        return {token_, static_cast<ItemReceiver&&>(rcvr)};
      }
    };

    template <class Snd1, class Snd2>
    using finally_t = decltype(exec::finally(std::declval<Snd1>(), std::declval<Snd2>()));

    template <class Token, class Receiver>
    struct operation_base : operation_rcvr_base<Receiver> {
      using operation_rcvr_base<Receiver>::operation_rcvr_base;

      std::optional<Token> token_{};
      std::optional<stdexec::connect_result_t<
        finally_t<exec::next_sender_of_t<Receiver, run_sender<Token>>, close_sender_of_t<Token>>,
        final_receiver<Receiver>>>
        run_op_{};
    };

    template <class Token, class Receiver>
    struct open_receiver {
      operation_base<Token, Receiver>* op_;

      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }

      void set_value(stdexec::set_value_t, Token token) && noexcept {
        try {
          Token& t = op_->token_.emplace(static_cast<Token&&>(token));
          auto& run_op = op_->run_op_.emplace(stdexec::__conv{[&] {
            return stdexec::connect(
              exec::finally(exec::set_next(op_->rcvr_, run_sender<Token>{op_->token_}), close(t)),
              final_receiver<Receiver>{op_});
          }});
          stdexec::start(run_op);
        } catch (...) {
          stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), std::current_exception());
        }
      }

      template <class Err>
      void set_error(stdexec::set_error_t, Err&& err) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), static_cast<Err&&>(err));
      }

      void set_stopped(stdexec::set_stopped_t) && noexcept {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->rcvr_));
      }
    };

    template <class Resource, class Receiver>
    struct operation : operation_base<token_of_t<Resource, stdexec::env_of_t<Receiver>>, Receiver> {
      using Token = token_of_t<Resource, stdexec::env_of_t<Receiver>>;
      stdexec::connect_result_t<open_sender_of_t<Resource>, open_receiver<Token, Receiver>>
        open_op_{};

      operation(Resource& resource, Receiver rcvr)
        : operation_base<Token, Receiver>{static_cast<Receiver&&>(rcvr)}
        , open_op_{stdexec::connect(open(resource), open_receiver<Token, Receiver>{this})} {
      }

      void start(stdexec::start_t) noexcept {
        stdexec::start(open_op_);
      }
    };

    template <class Resource>
    struct sequence {
      using is_sender = exec::sequence_tag;

      Resource& resource_;

      template <class Receiver>
      auto subscribe(exec::subscribe_t, Receiver rcvr) const
        noexcept(nothrow_decay_copyable<Receiver>) -> operation<Resource, Receiver> {
        return {resource_, static_cast<Receiver&&>(rcvr)};
      }

      template <class Env>
      auto get_completion_signatures(stdexec::get_completion_signatures_t, Env) const noexcept
        -> stdexec::make_completion_signatures<
          open_sender_of_t<Resource>,
          Env,
          stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>>;
    };

    struct run_t;
    extern const run_t run;

    template <class Resource>
    concept has_run_member_cpo = requires(Resource& resource) { resource.run(run); };

    template <class Resource>
    concept has_static_run_member_cpo = requires(Resource& resource) {
      Resource::run(resource, run);
    };

    template <class Resource>
    concept has_run_member = has_run_member_cpo<Resource> || has_static_run_member_cpo<Resource>;

    template <class Resource>
    concept no_run_member = !has_run_member<Resource>;

    struct run_t {
      template <has_run_member Resource>
      auto operator()(Resource& resource) const noexcept {
        if constexpr (has_run_member_cpo<Resource>) {
          return resource.run(run);
        } else {
          return Resource::run(resource, run);
        }
      }

      template <no_run_member Resource>
        requires with_open_and_close<Resource>
      auto operator()(Resource& resource) const noexcept -> sequence<Resource> {
        return {resource};
      }
    };

    inline constexpr run_t run{};
  }

  using async_resource_::run_t;
  using async_resource_::run;

  template <class _Resource>
  concept resource = requires(_Resource&& __resource) { run(__resource); };

  template <resource _Resource, class _Env = stdexec::empty_env>
  using resource_token_of_t =
    stdexec::__single_sender_value_t< call_result_t<run_t, _Resource&>, _Env>;

  struct use_resources_t {
    template <class Fn, class... Resources>
    auto operator()(Fn&& fun, Resources&... resources) const {
      return sio::first(
        sio::let_value_each(sio::zip(sio::run(resources)...), static_cast<Fn&&>(fun)));
    }
  };

  inline constexpr use_resources_t use_resources{};
}
