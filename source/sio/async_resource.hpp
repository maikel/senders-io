/*
 * Copyright (c) 2024 Maikel Nadolski
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
#include <exec/sequence_senders.hpp>
#include <stdexec/__detail/__transform_completion_signatures.hpp>

namespace sio::async {
  namespace async_resource_ {
    struct open_t;
    extern const open_t open;

    template <class Tp>
    concept has_open_member_cpo = requires(Tp&& obj) { static_cast<Tp&&>(obj).open(); };

    template <class Tp>
    concept nothrow_open_member_cpo = requires(Tp&& obj) {
      { static_cast<Tp&&>(obj).open() } noexcept;
    };

    template <class Tp>
    concept has_open_static_member_cpo = requires(Tp&& obj) { Tp::open(static_cast<Tp&&>(obj)); };

    template <class Tp>
    concept nothrow_open_static_member_cpo = requires(Tp&& obj) {
      { Tp::open(static_cast<Tp&&>(obj)) } noexcept;
    };

    struct open_t {
      template <class Res>
        requires has_open_member_cpo<Res&> || has_open_static_member_cpo<Res&>
      auto operator()(Res& resource) const
        noexcept(nothrow_open_member_cpo<Res&> || nothrow_open_static_member_cpo<Res&>) {
        if constexpr (has_open_member_cpo<Res&>) {
          return resource.open();
        } else {
          return Res::open(resource);
        }
      }
    };

    struct close_t;
    extern const close_t close;

    template <class Tp>
    concept has_close_member_cpo = requires(Tp&& obj) { static_cast<Tp&&>(obj).close(); };

    template <class Tp>
    concept nothrow_close_member_cpo = requires(Tp&& obj) {
      { static_cast<Tp&&>(obj).close() } noexcept;
    };

    template <class Tp>
    concept has_close_static_member_cpo = requires(Tp&& obj) { Tp::close(static_cast<Tp&&>(obj)); };

    template <class Tp>
    concept nothrow_close_static_member_cpo = requires(Tp&& obj) {
      { Tp::close(static_cast<Tp&&>(obj)) } noexcept;
    };

    struct close_t {
      template <class Token>
        requires has_close_member_cpo<Token&> || has_close_static_member_cpo<Token&>
      auto operator()(const Token& token) const noexcept(
        nothrow_close_member_cpo<const Token&> || nothrow_close_static_member_cpo<const Token&>) {
        if constexpr (has_close_member_cpo<const Token&>) {
          return token.close();
        } else {
          return Token::close(token);
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
    { open(resource) } -> stdexec::__single_value_sender<Env>;
  };

  template <class Resource, class Env>
    requires with_open<Resource, Env>
  using token_of_t = stdexec::__single_sender_value_t<call_result_t<open_t, Resource&>, Env>;

  template <class Sndr, class Env = stdexec::env<>>
  concept sender_of_void = stdexec::sender_of<Sndr, stdexec::set_value_t()> && requires {
    typename stdexec::__value_types_t<
      stdexec::__completion_signatures_of_t<Sndr, Env>,
      stdexec::__msingle_or<void>,
      stdexec::__msingle_or<void>>;
  };

  template <class Resource, class Env = stdexec::env<>>
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
      using receiver_concept = stdexec::receiver_t;
      operation_rcvr_base<Receiver>* op_;

      auto get_env() const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(op_->rcvr_);
      }

      void set_value() && noexcept {
        stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
      }

      template <class Err>
      void set_error(Err&& err) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), static_cast<Err&&>(err));
      }

      void set_stopped() && noexcept {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->rcvr_));
      }
    };

    template <class Token, class ItemReceiver>
    struct use_operation {
      std::optional<Token>& token_;
      [[no_unique_address]] ItemReceiver rcvr_;

      void start() noexcept {
        stdexec::set_value(static_cast<ItemReceiver&&>(rcvr_), static_cast<Token&&>(*token_));
      }
    };

    template <class Token>
    struct use_sender {
      using sender_concept = stdexec::sender_t;

      std::optional<Token>& token_;

      using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t(Token&&)>;

      template <class ItemReceiver>
      auto connect(ItemReceiver rcvr) noexcept(
        nothrow_decay_copyable<ItemReceiver>) -> use_operation<Token, ItemReceiver> {
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
        finally_t<exec::next_sender_of_t<Receiver, use_sender<Token>>, close_sender_of_t<Token>>,
        final_receiver<Receiver>>>
        use_op_{};
    };

    template <class Token, class Receiver>
    struct open_receiver {
      using receiver_concept = stdexec::receiver_t;

      operation_base<Token, Receiver>* op_;

      auto get_env() const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(op_->rcvr_);
      }

      void set_value(Token token) && noexcept {
        try {
          Token& t = op_->token_.emplace(static_cast<Token&&>(token));
          auto& use_op = op_->use_op_.emplace(stdexec::__emplace_from{[&] {
            return stdexec::connect(
              exec::finally(exec::set_next(op_->rcvr_, use_sender<Token>{op_->token_}), close(t)),
              final_receiver<Receiver>{op_});
          }});
          stdexec::start(use_op);
        } catch (...) {
          stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), std::current_exception());
        }
      }

      template <class Err>
      void set_error(Err&& err) && noexcept {
        stdexec::set_error(static_cast<Receiver&&>(op_->rcvr_), static_cast<Err&&>(err));
      }

      void set_stopped() && noexcept {
        exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->rcvr_));
      }
    };

    template <class Resource, class Receiver>
    struct operation : operation_base<token_of_t<Resource, stdexec::env_of_t<Receiver>>, Receiver> {
      using Token = token_of_t<Resource, stdexec::env_of_t<Receiver>>;
      stdexec::connect_result_t<open_sender_of_t<Resource>, open_receiver<Token, Receiver>>
        open_op_{};

      operation(Resource resource, Receiver rcvr)
        : operation_base<Token, Receiver>{static_cast<Receiver&&>(rcvr)}
        , open_op_{stdexec::connect(open(resource), open_receiver<Token, Receiver>{this})} {
      }

      void start() noexcept {
        stdexec::start(open_op_);
      }
    };

    template <class Resource>
    struct sequence {
      using sender_concept = exec::sequence_sender_t;

      Resource resource_;

      template <class Receiver>
      friend auto tag_invoke(exec::subscribe_t, sequence&& self, Receiver rcvr) noexcept(
        nothrow_decay_copyable<Receiver>) -> operation<Resource, Receiver> {
        return {self.resource_, static_cast<Receiver&&>(rcvr)};
      }

      template <class Env>
      auto get_completion_signatures(Env&&) const noexcept
        -> stdexec::transform_completion_signatures_of<
          open_sender_of_t<Resource>,
          Env,
          stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>,
          stdexec::__mconst<stdexec::completion_signatures<>>::__f>;

      template <class Env>
      friend auto tag_invoke(exec::get_item_types_t, sequence&& self, Env&&) noexcept
        -> exec::item_types<use_sender<token_of_t<Resource, Env>>> {
        return {};
      }
    };

    struct use_t;
    extern const use_t use;

    template <class Resource>
    concept has_use_member_cpo = requires(Resource&& resource) {
      std::forward<Resource>(resource).use();
    };

    template <class Resource>
    concept has_static_use_member_cpo = requires(Resource&& resource) {
      Resource::use(std::forward<Resource>(resource));
    };

    template <class Resource>
    concept has_use_member = has_use_member_cpo<Resource> || has_static_use_member_cpo<Resource>;

    template <class Resource>
    concept no_use_member = !has_use_member<Resource>;

    struct use_t {
      template <has_use_member Resource>
      auto operator()(Resource&& resource) const noexcept {
        if constexpr (has_use_member_cpo<Resource>) {
          return resource.use();
        } else {
          return Resource::use(resource);
        }
      }

      template <no_use_member Resource>
        requires with_open_and_close<Resource>
      auto operator()(const Resource& resource) const noexcept -> sequence<Resource> {
        return {resource};
      }
    };

    inline constexpr use_t use{};
  }

  using async_resource_::use_t;
  using async_resource_::use;

  template <class Resource>
  concept resource = requires(Resource&& __resource) { use(__resource); };

  template <class Sequence, class Env>
  using item_completion_signatures_of_t = //
    stdexec::__mapply<
      stdexec::__mbind_back_q<stdexec::completion_signatures_of_t, Env>,
      exec::item_types_of_t<Sequence, Env>>;

  template <class _Sequence, class _Env>
  using single_item_value_t = stdexec::__gather_completions<
    stdexec::set_value_t,
    item_completion_signatures_of_t<_Sequence, _Env>,
    stdexec::__msingle_or<void>,
    stdexec::__q<stdexec::__msingle>>;

  template <resource Resource, class Env = stdexec::env<>>
  using resource_token_of_t = decay_t<single_item_value_t<call_result_t<use_t, Resource&>, Env>>;

  struct use_resources_t {
    template <class Fn, class... DeferredResources>
    auto operator()(Fn&& fn, DeferredResources&&... resources) const {
      return sio::first(
        sio::let_value_each(sio::zip(sio::async::use(resources)...), static_cast<Fn&&>(fn)));
    }
  };

  inline constexpr use_resources_t use_resources{};
}
