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

#include "./async_mutex.hpp"
#include "./async_resource.hpp"
#include "./deferred.hpp"

#include "./intrusive_list.hpp"
#include "./sequence/any_sequence_of.hpp"

#include <exec/async_scope.hpp>

namespace sio {
  namespace channel_ {
    struct observer {
      any_sequence_receiver_ref<stdexec::completion_signatures<stdexec::set_value_t()>> receiver;
      observer* prev = nullptr;
      observer* next = nullptr;
      std::atomic<bool> started_;
    };

    struct context {
      async_mutex mutex_{};
      intrusive_list<&observer::prev, &observer::next> observers_{};
      exec::async_scope scope_{};
    };

    struct handle_base {
      context* resource;

      template <class Item>
      auto notify_all(Item item) {
        return stdexec::let_value(
          stdexec::when_all(stdexec::just(std::move(item)), resource->mutex_.lock()),
          [resource = resource](const Item& item) {
            for (observer& o: resource->observers_) {
              resource->scope_.spawn(
                exec::set_next(o.receiver, item) //
                | stdexec::upon_stopped([resource, &o] {
                    resource->observers_.erase(&o);
                    stdexec::set_value(std::move(o.receiver));
                  }));
            }
            return resource->scope_.on_empty();
          });
      }

      auto unsubscribe(observer* o) {
        return stdexec::then(resource->mutex_.lock(), [resource = resource, o]() noexcept {
          resource->observers_.erase(o);
          stdexec::set_value(std::move(o->receiver));
        });
      }

      auto subscribe(observer* o) {
        return stdexec::then(resource->mutex_.lock(), [resource = resource, o]() noexcept {
          o->started_.store(true, std::memory_order_relaxed);
          resource->observers_.push_back(o);
        });
      }

      auto close() const {
        return stdexec::let_value(resource->mutex_.lock(), [resource = resource]() noexcept {
          while (!resource->observers_.empty()) {
            observer* o = resource->observers_.pop_front();
            stdexec::set_value(std::move(o->receiver));
          }
          return resource->scope_.on_empty();
        });
      }
    };

    template <class Receiver>
    struct subscribe_operation;

    template <class Receiver>
    struct on_stop_requested {
      subscribe_operation<Receiver>* op_;
      void operator()() const noexcept;
    };

    template <class Receiver>
    struct wrap_receiver {
      using is_receiver = void;
      subscribe_operation<Receiver>* op_;
      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept;

      template <class Sender>
      exec::next_sender_of_t<Receiver, Sender> set_next(exec::set_next_t, Sender&& sndr);
      void set_value(stdexec::set_value_t) && noexcept;
    };

    template <class Receiver>
    struct stop_receiver {
      using is_receiver = void;
      subscribe_operation<Receiver>* op_;

      stdexec::empty_env get_env(stdexec::get_env_t) const noexcept {
        return {};
      }

      void set_value(stdexec::set_value_t) && noexcept;
    };

    struct nop_receiver {
      using is_receiver = void;

      stdexec::empty_env get_env(stdexec::get_env_t) const noexcept {
        return {};
      }

      void set_value(stdexec::set_value_t) const noexcept {
      }
    };

    using notify_sender_t = decltype(std::declval<handle_base>().notify_all());

    using subscribe_sender_t =
      decltype(std::declval<handle_base>().subscribe(std::declval<observer*>()));

    using stop_sender_t =
      decltype(std::declval<handle_base>().unsubscribe(std::declval<observer*>()));

    template <class Receiver>
    struct subscribe_operation {
      Receiver rcvr_;
      wrap_receiver<Receiver> wrapped_receiver_;
      observer observer_;
      handle_base channel_;
      stdexec::connect_result_t<subscribe_sender_t, nop_receiver> subscribe_operation_;
      stdexec::connect_result_t<stop_sender_t, stop_receiver<Receiver>> stop_operation_;
      using stop_token = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;
      using callback_type =
        typename stop_token::template callback_type<on_stop_requested<Receiver>>;
      std::atomic<int> n_ops_{0};
      std::optional<callback_type> callback_{};

      subscribe_operation(Receiver&& rcvr, handle_base channel)
        : rcvr_(std::move(rcvr))
        , wrapped_receiver_{this}
        , observer_{wrapped_receiver_}
        , channel_{channel}
        , subscribe_operation_{stdexec::connect(channel_.subscribe(&observer_), nop_receiver{})}
        , stop_operation_{
            stdexec::connect(channel_.unsubscribe(&observer_), stop_receiver<Receiver>{this})} {
      }

      void start(stdexec::start_t) noexcept {
        // TODO racy
        callback_.emplace(
          stdexec::get_stop_token(stdexec::get_env(rcvr_)), on_stop_requested{this});
        int expected = 0;
        if (n_ops_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          stdexec::start(subscribe_operation_);
        }
      }
    };

    template <class Receiver>
    void on_stop_requested<Receiver>::operator()() const noexcept {
      int before = op_->n_ops_.exchange(2, std::memory_order_relaxed);
      if (before == 1) {
        stdexec::start(op_->stop_operation_);
      } else if (before == 0) {
        op_->callback_.reset();
        stdexec::set_value(std::move(op_->rcvr_));
      }
    }

    template <class Receiver>
    stdexec::env_of_t<Receiver>
      wrap_receiver<Receiver>::get_env(stdexec::get_env_t) const noexcept {
      return stdexec::get_env(op_->rcvr_);
    }

    template <class Receiver>
    template <class Sender>
    exec::next_sender_of_t<Receiver, Sender>
      wrap_receiver<Receiver>::set_next(exec::set_next_t, Sender&& sndr) {
      return exec::set_next(op_->rcvr_, std::forward<Sender>(sndr));
    }

    template <class Receiver>
    void wrap_receiver<Receiver>::set_value(stdexec::set_value_t) && noexcept {
      int before = op_->n_ops_.exchange(3, std::memory_order_relaxed);
      if (before == 1) {
        op_->callback_.reset();
        stdexec::set_value(std::move(op_->rcvr_));
      }
    }

    template <class Receiver>
    void stop_receiver<Receiver>::set_value(stdexec::set_value_t) && noexcept {
      stdexec::set_value(std::move(op_->rcvr_));
    }

    struct subscribe_sequence {
      using is_sender = exec::sequence_tag;
      using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t()>;

      template <class Receiver>
      auto subscribe(exec::subscribe_t, Receiver rcvr) const noexcept
        -> subscribe_operation<Receiver> {
        return {std::move(rcvr), channel_};
      }

      handle_base channel_;
    };

    class channel;

    class handle {
     private:
      handle_base base_;

      friend class channel;

      explicit handle(context& ctx)
        : base_{&ctx} {
      }

      friend class async::close_t;

      auto close(async::close_t) const {
        return base_.close();
      }

     public:
      notify_sender_t notify_all() {
        return base_.notify_all();
      }

      subscribe_sequence subscribe() const noexcept {
        return {base_};
      }
    };

    struct channel {
      auto open(async::open_t) const {
        return stdexec::let_value(
          stdexec::just(make_deferred<context>()), [this](auto& ctx) noexcept {
            ctx();
            return stdexec::just(handle{*ctx});
          });
      }
    };
  }

  using async_channel = channel_::channel;
  using async_channel_handle = channel_::handle;
}