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

#include "../concepts.hpp"
#include "./sequence_concepts.hpp"

#include <exec/trampoline_scheduler.hpp>

#include <ranges>

namespace sio {
  namespace iterate_ {
    using namespace stdexec;

    template <class Iterator, class Sentinel>
    struct operation_base {
      [[no_unique_address]] Iterator iterator_;
      [[no_unique_address]] Sentinel sentinel_;
    };

    template <class Range>
    using operation_base_t =
      operation_base<std::ranges::iterator_t<Range>, std::ranges::sentinel_t<Range>>;

    template <class Iterator, class Sentinel, class ItemRcvr>
    struct item_operation {
      [[no_unique_address]] ItemRcvr rcvr_;
      operation_base<Iterator, Sentinel>* parent_;

      void start(start_t) noexcept {
        stdexec::set_value(static_cast<ItemRcvr&&>(rcvr_), *parent_->iterator_++);
      }
    };

    template <class Iterator, class Sentinel>
    struct sender {
      using is_sender = void;
      using completion_signatures =
        stdexec::completion_signatures<set_value_t(std::iter_reference_t<Iterator>)>;
      operation_base<Iterator, Sentinel>* parent_;

      template <__decays_to<sender> Self, receiver_of<completion_signatures> ItemRcvr>
      static auto connect(Self&& self, connect_t, ItemRcvr rcvr) //
        noexcept(__nothrow_decay_copyable<ItemRcvr>)
          -> item_operation<Iterator, Sentinel, ItemRcvr> {
        return {static_cast<ItemRcvr&&>(rcvr), self.parent_};
      }
    };

    template <class Range>
    using sender_t = sender<std::ranges::iterator_t<Range>, std::ranges::sentinel_t<Range>>;

    template <class Range, class Receiver>
    struct operation;

    template <class Range, class Receiver>
    struct next_receiver {
      using is_receiver = void;
      operation<Range, Receiver>* op_;

      void set_value(set_value_t) && noexcept {
        op_->start_next();
      }

      void set_stopped(set_stopped_t) && noexcept {
        if constexpr (unstoppable_token<stop_token_of_t<env_of_t<Receiver>>>) {
          stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
        } else {
          auto token = stdexec::get_stop_token(stdexec::get_env(op_->rcvr_));
          if (token.stop_requested()) {
            stdexec::set_stopped(static_cast<Receiver&&>(op_->rcvr_));
          } else {
            stdexec::set_value(static_cast<Receiver&&>(op_->rcvr_));
          }
        }
      }

      env_of_t<Receiver> get_env(get_env_t) const noexcept {
        return stdexec::get_env(op_->rcvr_);
      }
    };

    template <class Range, class Receiver>
    struct operation : operation_base_t<Range> {
      Receiver rcvr_;

      using ItemSender = decltype(stdexec::on(
        std::declval<exec::trampoline_scheduler&>(),
        std::declval<sender_t<Range>>()));

      std::optional<connect_result_t<
        exec::__next_sender_of_t<Receiver, ItemSender>,
        next_receiver<Range, Receiver>>>
        op_{};
      exec::trampoline_scheduler scheduler_{};

      void start_next() noexcept {
        if (this->iterator_ == this->sentinel_) {
          stdexec::set_value(static_cast<Receiver&&>(rcvr_));
        } else {
          try {
            stdexec::start(op_.emplace(__conv{[&] {
              return stdexec::connect(
                exec::set_next(rcvr_, stdexec::on(scheduler_, sender_t<Range>{this})),
                next_receiver<Range, Receiver>{this});
            }}));
          } catch (...) {
            stdexec::set_error(static_cast<Receiver&&>(rcvr_), std::current_exception());
          }
        }
      }

      void start(start_t) noexcept {
        start_next();
      }
    };

    template <class Range>
    struct sequence {
      using is_sender = exec::sequence_tag;

      using completion_signatures = stdexec::completion_signatures<
        set_value_t(std::ranges::range_reference_t<Range>),
        set_error_t(std::exception_ptr),
        set_stopped_t()>;

      [[no_unique_address]] Range range_;

      template <
        __decays_to<sequence> Self,
        exec::sequence_receiver_of<completion_signatures> Receiver>
        requires sender_to<
          exec::__next_sender_of_t<Receiver, sender_t<Range>>,
          next_receiver<Range, Receiver>>
      static operation<Range, Receiver>
        sequence_connect(Self&& self, exec::sequence_connect_t, Receiver rcvr) noexcept(
          __nothrow_decay_copyable<Receiver>) {
        return {
          {std::ranges::begin(self.range_), std::ranges::end(self.range_)},
          static_cast<Receiver&&>(rcvr)
        };
      }

      auto get_sequence_env(exec::get_sequence_env_t) const noexcept {
        if constexpr (std::ranges::sized_range<Range>) {
          return __make_env(
            __with_(exec::parallelism, exec::lock_step),
            __with_(exec::cardinality, std::ranges::size(range_)));
        } else {
          return __make_env(__with_(exec::parallelism, exec::lock_step));
        }
      }
    };

    struct iterate_t {
      template <std::ranges::range Range>
      sequence<Range> operator()(Range&& range) const noexcept {
        return {static_cast<Range&&>(range)};
      }
    };
  }

  using iterate_::iterate_t;
  inline constexpr iterate_t iterate;
}