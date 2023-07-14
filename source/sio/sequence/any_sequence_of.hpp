/* Copyright (c) 2023 Maikel Nadolski
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

#include "exec/any_sender_of.hpp"

#include "../concepts.hpp"
#include "./sequence_concepts.hpp"

namespace sio {
  namespace any_ {
    namespace next_ {
      template <stdexec::__is_completion_signatures Sigs>
      struct rcvr_next_vfun {
        using return_sigs =
          stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;
        using void_sender = typename exec::any_receiver_ref<return_sigs>::template any_sender<>;
        using item_sender = typename exec::any_receiver_ref<Sigs>::template any_sender<>;
        void_sender (*fn_)(void*, item_sender&&);
      };

      template <class Rcvr>
      struct rcvr_next_vfun_fn {
        using return_sigs =
          stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;
        using void_sender = typename exec::any_receiver_ref<return_sigs>::template any_sender<>;

        template <class Sigs>
        using item_sender = typename exec::any_receiver_ref<Sigs>::template any_sender<>;

        template <stdexec::__is_completion_signatures Sigs>
        constexpr void_sender (*operator()(Sigs*) const)(void*, item_sender<Sigs>&&) {
          return +[](void* r, item_sender<Sigs>&& sndr) noexcept -> void_sender {
            return void_sender{
              exec::set_next(*static_cast<Rcvr*>(r), static_cast<item_sender<Sigs>&&>(sndr))};
          };
        }
      };

      template <class NextSigs, class Sigs, class... Queries>
      struct next_vtable;

      template <class NextSigs, class... Sigs, class... Queries>
      struct next_vtable<NextSigs, stdexec::completion_signatures<Sigs...>, Queries...>
        : public rcvr_next_vfun<NextSigs>
        , public exec::__any::__rec::__rcvr_vfun<Sigs>...
        , public exec::__any::__query_vfun<Queries>... {
        using exec::__any::__query_vfun<Queries>::operator()...;

        template <class Rcvr>
          requires exec::sequence_receiver_of<Rcvr, NextSigs>
                && (callable<exec::__any::__query_vfun_fn<Rcvr>, Queries> && ...)
        static const next_vtable* __create_vtable(exec::__any::__create_vtable_t) noexcept {
          static const next_vtable vtable_{
            {rcvr_next_vfun_fn<Rcvr>{}((NextSigs*) nullptr)},
            {exec::__any::__rec::__rcvr_vfun_fn<Rcvr>{}((Sigs*) nullptr)}...,
            {exec::__any::__query_vfun_fn<Rcvr>{}((Queries) nullptr)}...};
          return &vtable_;
        }
      };

      template <class Sigs, class... Queries>
      struct receiver_ref;

      template <class... Sigs, class... Queries>
      struct receiver_ref<stdexec::completion_signatures<Sigs...>, Queries...> {
        using return_sigs =
          stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;
        using void_sender = typename exec::any_receiver_ref<return_sigs>::template any_sender<>;
        using next_sigs = stdexec::completion_signatures<Sigs...>;
        using compl_sigs = exec::__sequence_completion_signatures_of_t<
          exec::__sequence_sndr::__unspecified_sender_of<next_sigs>,
          stdexec::empty_env>;
        using item_sender = typename exec::any_receiver_ref<next_sigs>::template any_sender<>;

        using vtable_t = next_vtable<next_sigs, compl_sigs, Queries...>;

        template <class Sig>
        using vfun = exec::__any::__rec::__rcvr_vfun<Sig>;

        struct env_t {
          const vtable_t* vtable_;
          void* rcvr_;
          stdexec::in_place_stop_token token_;

          template <class Tag, stdexec::same_as<env_t> Self, class... As>
            requires callable<const vtable_t&, Tag, void*, As...>
          friend auto tag_invoke(Tag, const Self& self, As&&... as) noexcept(
            nothrow_callable<const vtable_t&, Tag, void*, As...>)
            -> call_result_t<const vtable_t&, Tag, void*, As...> {
            return (*self.vtable_)(Tag{}, self.rcvr_, (As&&) as...);
          }

          friend stdexec::in_place_stop_token
            tag_invoke(stdexec::get_stop_token_t, const env_t& self) noexcept {
            return self.token_;
          }
        } env_;

        using is_receiver = void;

        template <stdexec::__none_of<receiver_ref, const receiver_ref, env_t, const env_t> Rcvr>
          requires exec::sequence_receiver_of<Rcvr, stdexec::completion_signatures<Sigs...>>
                && (callable<exec::__any::__query_vfun_fn<Rcvr>, Queries> && ...)
        receiver_ref(Rcvr& __rcvr) noexcept
          : env_{
            exec::__any::__create_vtable(stdexec::__mtype<vtable_t>{}, stdexec::__mtype<Rcvr>{}),
            &__rcvr,
            stdexec::get_stop_token(stdexec::get_env(__rcvr))} {
        }

        template <class Sender>
        void_sender set_next(exec::set_next_t, Sender&& sndr) {
          return (*static_cast<const rcvr_next_vfun<next_sigs>*>(env_.vtable_)->fn_)(
            env_.rcvr_, static_cast<Sender&&>(sndr));
        }

        template <class... As>
          requires stdexec::__v<
            stdexec::__mapply<stdexec::__contains<stdexec::set_value_t(As...)>, compl_sigs>>
        void set_value(stdexec::set_value_t, As&&... as) && noexcept {
          (*static_cast<const vfun<stdexec::set_value_t(As...)>*>(env_.vtable_)->__fn_)(
            env_.rcvr_, static_cast<As&&>(as)...);
        }

        template <class Error>
          requires stdexec::__v<
            stdexec::__mapply<stdexec::__contains<stdexec::set_error_t(Error)>, compl_sigs>>
        void set_error(stdexec::set_error_t, Error&& error) && noexcept {
          (*static_cast<const vfun<stdexec::set_error_t(Error)>*>(env_.vtable_)->__fn_)(
            env_.rcvr_, static_cast<Error&&>(error));
        }

        void set_stopped(stdexec::set_stopped_t) && noexcept
        {
          if constexpr (stdexec::__v<
            stdexec::__mapply<stdexec::__contains<stdexec::set_stopped_t()>, compl_sigs>>) {
              if (env_.token_.stop_requested()) {
                (*static_cast<const vfun<stdexec::set_stopped_t()>*>(env_.vtable_)->__fn_)(env_.rcvr_);
              } else {
                (*static_cast<const vfun<stdexec::set_value_t()>*>(env_.vtable_)->__fn_)(env_.rcvr_);
              }
          } else {
            (*static_cast<const vfun<stdexec::set_value_t()>*>(env_.vtable_)->__fn_)(env_.rcvr_);
          }
        }

        const env_t& get_env(stdexec::get_env_t) const noexcept {
          return env_;
        }
      };
    }

    template <class Sigs, class Queries>
    using next_receiver_ref =
      stdexec::__mapply<stdexec::__mbind_front<stdexec::__q<next_::receiver_ref>, Sigs>, Queries>;

    template <
      class Sigs,
      class SenderQueries = stdexec::__types<>,
      class ReceiverQueries = stdexec::__types<>>
    struct sequence_sender {
      using receiver_ref_t = next_receiver_ref<Sigs, ReceiverQueries>;
      using query_vtable = exec::__any::__query_vtable<SenderQueries>;

      class vtable : public query_vtable {
       public:
        const query_vtable& queries() const noexcept {
          return *this;
        }

        exec::__any::__immovable_operation_storage (*subscribe_)(void*, receiver_ref_t);

        template <class Sender>
          requires exec::sequence_sender_to<Sender, receiver_ref_t>
        static const vtable* __create_vtable(exec::__any::__create_vtable_t) noexcept {
          static const vtable vtable_{
            {*exec::__any::__create_vtable(
              stdexec::__mtype<query_vtable>{}, stdexec::__mtype<Sender>{})},
            [](void* object_pointer, receiver_ref_t receiver)
              -> exec::__any::__immovable_operation_storage {
              Sender& sender = *static_cast<Sender*>(object_pointer);
              using op_state_t = exec::subscribe_result_t<Sender, receiver_ref_t>;
              return exec::__any::__immovable_operation_storage{
                std::in_place_type<op_state_t>, stdexec::__conv{[&] {
                  return exec::subscribe(
                    static_cast<Sender&&>(sender), static_cast<receiver_ref_t&&>(receiver));
                }}};
            }};
          return &vtable_;
        }
      };

      class env_t {
       public:
        env_t(const vtable* vtable, void* sender) noexcept
          : vtable_{vtable}
          , sender_{sender} {
        }
       private:
        const vtable* vtable_;
        void* sender_;

        template <class Tag, class... As>
          requires callable<const query_vtable&, Tag, void*, As...>
        friend auto tag_invoke(Tag, const env_t& self, As&&... as) noexcept(
          nothrow_callable<const query_vtable&, Tag, void*, As...>)
          -> call_result_t<const query_vtable&, Tag, void*, As...> {
          return self.vtable_->queries()(Tag{}, self.sender_, (As&&) as...);
        }
      };

      class __t {
       public:
        using completion_signatures = Sigs;
        using is_sender = exec::sequence_tag;

        __t(const __t&) = delete;
        __t& operator=(const __t&) = delete;

        __t(__t&&) = default;
        __t& operator=(__t&&) = default;

        template <stdexec::__not_decays_to<__t> Sender>
          requires exec::sequence_sender_to<Sender, receiver_ref_t>
        __t(Sender&& sndr)
          : storage_{(Sender&&) sndr} {
        }

        exec::__any::__immovable_operation_storage __connect(receiver_ref_t receiver) {
          return storage_.__get_vtable()->subscribe_(storage_.__get_object_pointer(), receiver);
        }

        exec::__any::__unique_storage_t<vtable> storage_;

        template <exec::sequence_receiver_of<Sigs> Rcvr>
        stdexec::__t<exec::__any::__operation<stdexec::__id<Rcvr>, true>>
          subscribe(exec::subscribe_t, Rcvr __rcvr) && {
          return {static_cast<__t&&>(*this), static_cast<Rcvr&&>(__rcvr)};
        }

        env_t get_env(stdexec::get_env_t) const noexcept {
          return {storage_.__get_vtable(), storage_.__get_object_pointer()};
        }
      };
    };
  }

  template <class Completions, auto... ReceiverQueries>
  class any_sequence_receiver_ref {
    using receiver_base = any_::next_receiver_ref<Completions, exec::queries<ReceiverQueries...>>;
    using env_t = stdexec::env_of_t<receiver_base>;
    receiver_base receiver_;
   public:
    using is_receiver = void;

    env_t get_env(stdexec::get_env_t) const noexcept {
      return stdexec::get_env(receiver_);
    }

    template <class Sender>
      requires callable<exec::set_next_t, receiver_base&, Sender>
    auto set_next(exec::set_next_t, Sender&& sender) noexcept {
      return exec::set_next(receiver_, static_cast<Sender&&>(sender));
    }

    void set_value(stdexec::set_value_t) && noexcept {
      stdexec::set_value(static_cast<receiver_base&&>(receiver_));
    }

    template <class Error>
    void set_error(stdexec::set_error_t, Error&& error) && noexcept
      requires callable<stdexec::set_error_t, receiver_base&&, Error>
    {
      stdexec::set_error(static_cast<receiver_base&&>(receiver_), static_cast<Error&&>(error));
    }

    void set_stopped(stdexec::set_stopped_t) && noexcept
      requires callable<stdexec::set_stopped_t, receiver_base&&>
    {
      stdexec::set_stopped(static_cast<receiver_base&&>(receiver_));
    }

    template <stdexec::__not_decays_to<any_sequence_receiver_ref> Receiver>
      requires exec::sequence_receiver_of<Receiver, Completions>
    any_sequence_receiver_ref(Receiver& receiver) noexcept
      : receiver_(receiver) {
    }

    template <auto... SenderQueries>
    class any_sender {
      using sender_base = stdexec::__t< any_::sequence_sender<
        Completions,
        exec::queries<SenderQueries...>,
        exec::queries<ReceiverQueries...>>>;
      sender_base sender_;

     public:
      using is_sender = exec::sequence_tag;
      using completion_signatures = typename sender_base::completion_signatures;

      template <stdexec::__not_decays_to<any_sender> Sender>
        requires stdexec::sender_in<Sender, env_t>
              && exec::sequence_sender_to<Sender, receiver_base>
      any_sender(Sender&& sender) noexcept(nothrow_constructible_from<sender_base, Sender>)
        : sender_((Sender&&) sender) {
      }

      template <class Rcvr>
        requires callable<exec::subscribe_t, sender_base&&, Rcvr>
      exec::subscribe_result_t<sender_base&&, Rcvr> subscribe(exec::subscribe_t, Rcvr __rcvr) && {
        return exec::subscribe(static_cast<sender_base&&>(sender_), static_cast<Rcvr&&>(__rcvr));
      }

      stdexec::env_of_t<sender_base> get_env(stdexec::get_env_t) const noexcept {
        return stdexec::get_env(sender_);
      }
    };
  };
}