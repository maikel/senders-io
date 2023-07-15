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
#include "./sequence/ignore_all.hpp"

#include <exec/async_scope.hpp>

namespace sio {
  namespace channel_ {
    template <class Completions>
    struct observer {
      any_sequence_receiver_ref<Completions> receiver;
      observer* prev = nullptr;
      observer* next = nullptr;
      std::atomic<bool> started_;
    };

    template <class Completions>
    struct context {
      async_mutex mutex_{};
      intrusive_list<&observer<Completions>::prev, &observer<Completions>::next> observers_{};
      exec::async_scope scope_{};
    };

    template <class Completions>
    struct handle_base {
      context<Completions>* resource;

      template <class Item>
      auto notify_all(Item item) const {
        return stdexec::let_value(
          stdexec::when_all(stdexec::just(std::move(item)), resource->mutex_.lock()),
          [resource = resource](const Item& item) {
            for (observer<Completions>& o: resource->observers_) {
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

      auto unsubscribe(observer<Completions>* o) const {
        return stdexec::then(resource->mutex_.lock(), [resource = resource, o]() noexcept {
          resource->observers_.erase(o);
          stdexec::set_value(std::move(o->receiver));
        });
      }

      auto subscribe(observer<Completions>* o) const {
        return stdexec::then(resource->mutex_.lock(), [resource = resource, o]() noexcept {
          o->started_.store(true, std::memory_order_relaxed);
          resource->observers_.push_back(o);
        });
      }

      auto close() const {
        return stdexec::let_value(resource->mutex_.lock(), [resource = resource]() noexcept {
          while (!resource->observers_.empty()) {
            observer<Completions>* o = resource->observers_.pop_front();
            stdexec::set_value(std::move(o->receiver));
          }
          return resource->scope_.on_empty();
        });
      }
    };

    template <class Completions, class Receiver>
    struct subscribe_operation;

    template <class Completions, class Receiver>
    struct on_stop_requested {
      subscribe_operation<Completions, Receiver>* op_;
      void operator()() const noexcept;
    };

    template <class Completions, class Receiver>
    struct wrap_receiver {
      using is_receiver = void;
      subscribe_operation<Completions, Receiver>* op_;
      stdexec::env_of_t<Receiver> get_env(stdexec::get_env_t) const noexcept;

      template <class Sender>
      exec::next_sender_of_t<Receiver, Sender> set_next(exec::set_next_t, Sender&& sndr);
      void set_value(stdexec::set_value_t) && noexcept;
    };

    template <class Completions, class Receiver>
    struct stop_receiver {
      using is_receiver = void;
      subscribe_operation<Completions, Receiver>* op_;

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

    template <class Completions>
    using subscribe_sender_t = decltype(std::declval<handle_base<Completions>>().subscribe(
      std::declval<observer<Completions>*>()));

    template <class Completions>
    using stop_sender_t = decltype(std::declval<handle_base<Completions>>().unsubscribe(
      std::declval<observer<Completions>*>()));

    template <class Completions, class Receiver>
    struct subscribe_operation {
      Receiver rcvr_;
      wrap_receiver<Completions, Receiver> wrapped_receiver_;
      observer<Completions> observer_;
      handle_base<Completions> channel_;
      stdexec::connect_result_t<subscribe_sender_t<Completions>, nop_receiver> subscribe_operation_;
      stdexec::connect_result_t<stop_sender_t<Completions>, stop_receiver<Completions, Receiver>>
        stop_operation_;
      using stop_token = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;
      using callback_type =
        typename stop_token::template callback_type<on_stop_requested<Completions, Receiver>>;
      std::atomic<int> n_ops_{0};
      std::optional<callback_type> callback_{};

      subscribe_operation(Receiver&& rcvr, handle_base<Completions> channel)
        : rcvr_(std::move(rcvr))
        , wrapped_receiver_{this}
        , observer_{wrapped_receiver_}
        , channel_{channel}
        , subscribe_operation_{stdexec::connect(channel_.subscribe(&observer_), nop_receiver{})}
        , stop_operation_{stdexec::connect(
            channel_.unsubscribe(&observer_),
            stop_receiver<Completions, Receiver>{this})} {
      }

      void start(stdexec::start_t) noexcept {
        callback_.emplace(
          stdexec::get_stop_token(stdexec::get_env(rcvr_)), on_stop_requested{this});
        int expected = 0;
        if (n_ops_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          stdexec::start(subscribe_operation_);
        }
      }
    };

    template <class Completions, class Receiver>
    void on_stop_requested<Completions, Receiver>::operator()() const noexcept {
      int before = op_->n_ops_.exchange(2, std::memory_order_relaxed);
      if (before == 1) {
        stdexec::start(op_->stop_operation_);
      } else if (before == 0) {
        op_->callback_.reset();
        stdexec::set_value(std::move(op_->rcvr_));
      }
    }

    template <class Completions, class Receiver>
    stdexec::env_of_t<Receiver>
      wrap_receiver<Completions, Receiver>::get_env(stdexec::get_env_t) const noexcept {
      return stdexec::get_env(op_->rcvr_);
    }

    template <class Completions, class Receiver>
    template <class Sender>
    exec::next_sender_of_t<Receiver, Sender>
      wrap_receiver<Completions, Receiver>::set_next(exec::set_next_t, Sender&& sndr) {
      return exec::set_next(op_->rcvr_, std::forward<Sender>(sndr));
    }

    template <class Completions, class Receiver>
    void wrap_receiver<Completions, Receiver>::set_value(stdexec::set_value_t) && noexcept {
      int before = op_->n_ops_.exchange(3, std::memory_order_relaxed);
      if (before == 1) {
        op_->callback_.reset();
        stdexec::set_value(std::move(op_->rcvr_));
      }
    }

    template <class Completions, class Receiver>
    void stop_receiver<Completions, Receiver>::set_value(stdexec::set_value_t) && noexcept {
      stdexec::set_value(std::move(op_->rcvr_));
    }

    template <class Completions>
    struct subscribe_sequence {
      using is_sender = exec::sequence_tag;
      using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t()>;

      template <class Receiver>
      auto subscribe(exec::subscribe_t, Receiver rcvr) const noexcept
        -> subscribe_operation<Completions, Receiver> {
        return {std::move(rcvr), channel_};
      }

      handle_base<Completions> channel_;
    };

    template <class Completions>
    class channel;

    template <class Completions>
    class handle {
     private:
      handle_base<Completions> base_;

      template <class>
      friend class channel;

      explicit handle(context<Completions>& ctx)
        : base_{&ctx} {
      }

      friend class async::close_t;

      auto close(async::close_t) const {
        return base_.close();
      }

     public:
      template <class Sequence>
      auto notify_all(Sequence&& seq) {
        return sio::transform_each(
                 std::forward<Sequence>(seq),
                 [base = base_]<class Item>(Item&& item) {
                   return base.notify_all(std::forward<Item>(item));
                 })
             | sio::ignore_all();
      }

      subscribe_sequence<Completions> subscribe() const noexcept {
        return {base_};
      }
    };

    template <class Completions>
    struct channel {
      auto open(async::open_t) const {
        return stdexec::let_value(
          stdexec::just(make_deferred<context<Completions>>()), [this](auto& ctx) noexcept {
            ctx();
            return stdexec::just(handle<Completions>{*ctx});
          });
      }
    };
  }

  template <class Completions>
  using async_channel = channel_::channel<Completions>;

  template <class Completions>
  using async_channel_handle = channel_::handle<Completions>;
}