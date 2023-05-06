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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdexec/execution.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/inline_scheduler.hpp>

#include "./address.hpp"

#include <string>
#include <string_view>

#include <netdb.h>
#include <signal.h>


namespace exec::net::ip {
  enum class gaierrc {
    invalid_flags = EAI_BADFLAGS,
    unknown_name = EAI_NONAME,
    temporary_failure = EAI_AGAIN,
    non_recoverable_failure = EAI_FAIL,
    family_not_supported = EAI_FAMILY,
    socktype_not_supported = EAI_SOCKTYPE,
    service_not_supported = EAI_SERVICE,
    memory_allocation_failure = EAI_MEMORY,
    system_error = EAI_SYSTEM,
    argument_buffer_overflow = EAI_OVERFLOW,
    no_address = EAI_NODATA,
    address_family_not_supported = EAI_ADDRFAMILY,
    in_progress = EAI_INPROGRESS,
    canceled = EAI_CANCELED,
    not_canceled = EAI_NOTCANCELED,
    all_done = EAI_ALLDONE,
    interrupted = EAI_INTR,
    idn_encode = EAI_IDN_ENCODE
  };
}

namespace std {
  template <>
  struct is_error_code_enum<exec::net::ip::gaierrc> : true_type { };
}

namespace exec::net::ip {
  struct resolver_error_category_t : std::error_category {
    const char* name() const noexcept override {
      return "resolver";
    }

    std::string message(int __ec) const override {
      return ::gai_strerror(__ec);
    }
  };

  const resolver_error_category_t& resolver_error_category() noexcept {
    static const resolver_error_category_t __impl{};
    return __impl;
  }
}

namespace exec::net::ip {
  enum class resolver_flags {
    canonical_name = AI_CANONNAME,
    passive = AI_PASSIVE,
    numeric_host = AI_NUMERICHOST,
    numeric_service = AI_NUMERICSERV,
    v4_mapped = AI_V4MAPPED,
    all_matching = AI_ALL,
    address_configured = AI_ADDRCONFIG
  };

  constexpr resolver_flags operator|(resolver_flags __lhs, resolver_flags __rhs) noexcept {
    return static_cast<resolver_flags>(static_cast<unsigned>(__lhs) | static_cast<unsigned>(__rhs));
  }

  constexpr resolver_flags operator&(resolver_flags __lhs, resolver_flags __rhs) noexcept {
    return static_cast<resolver_flags>(static_cast<unsigned>(__lhs) & static_cast<unsigned>(__rhs));
  }

  constexpr resolver_flags operator^(resolver_flags __lhs, resolver_flags __rhs) noexcept {
    return static_cast<resolver_flags>(static_cast<unsigned>(__lhs) ^ static_cast<unsigned>(__rhs));
  }

  constexpr resolver_flags operator~(resolver_flags __flags) noexcept {
    return static_cast<resolver_flags>(~static_cast<unsigned>(__flags));
  }

  using __addrinfo_type = ::addrinfo;

  template <class _IP>
  concept internet_protocol = requires(const _IP& __proto) {
    { __proto.family() };
    { __proto.type() };
    { __proto.protocol() };
  };

  class resolver_query {
   public:
    explicit resolver_query(
      const std::string& __service,
      resolver_flags __flags = resolver_flags::passive | resolver_flags::address_configured)
      : __host_name_{}
      , __service_name_{__service} {
      __hints_.ai_family = AF_UNSPEC;
      __hints_.ai_socktype = 0;
      __hints_.ai_protocol = 0;
      __hints_.ai_flags = static_cast<int>(__flags);
    }

    explicit resolver_query(
      const std::string& __host_name,
      const std::string& __service,
      resolver_flags __flags = resolver_flags::address_configured)
      : __host_name_{__host_name}
      , __service_name_{__service} {
      __hints_.ai_family = AF_UNSPEC;
      __hints_.ai_socktype = 0;
      __hints_.ai_protocol = 0;
      __hints_.ai_flags = static_cast<int>(__flags);
    }

    template <internet_protocol _InternetProtocol>
    explicit resolver_query(
      _InternetProtocol __protocol,
      const std::string& __service,
      resolver_flags __flags = resolver_flags::passive | resolver_flags::address_configured)
      : __host_name_{}
      , __service_name_{__service} {
      __hints_.ai_family = __protocol.family();
      __hints_.ai_socktype = __protocol.type();
      __hints_.ai_protocol = __protocol.protocol();
      __hints_.ai_flags = static_cast<int>(__flags);
    }

    template <internet_protocol _InternetProtocol>
    explicit resolver_query(
      _InternetProtocol __protocol,
      const std::string& __host,
      const std::string& __service,
      resolver_flags __flags = resolver_flags::address_configured)
      : __host_name_{__host}
      , __service_name_{__service} {
      __hints_.ai_family = __protocol.family();
      __hints_.ai_socktype = __protocol.type();
      __hints_.ai_protocol = __protocol.protocol();
      __hints_.ai_flags = static_cast<int>(__flags);
    }

    const __addrinfo_type& hints() const noexcept {
      return __hints_;
    }

    const std::string& host_name() const {
      return __host_name_;
    }

    const std::string& service_name() const {
      return __service_name_;
    }

   private:
    __addrinfo_type __hints_{};
    std::string __host_name_;
    std::string __service_name_;
  };

  class resolver_result {
   public:
    explicit resolver_result(
      __addrinfo_type* __result,
      std::string __host_name,
      std::string __service_name) noexcept
      : __host_name_{static_cast<std::string&&>(__host_name)}
      , __service_name_{static_cast<std::string>(__service_name)} {
      if (__result->ai_addr->sa_family == AF_INET) {
        ::sockaddr_in __native_endpoint = bit_cast<::sockaddr_in>(*__result->ai_addr);
        net::ip::address_v4 __addr{
          bit_cast<net::ip::address_v4::bytes_type>(__native_endpoint.sin_addr)};
        __endpoint_ = net::ip::endpoint{__addr, __native_endpoint.sin_port};
      } else {
        STDEXEC_ASSERT(__result->ai_addr->sa_family == AF_INET6);
        STDEXEC_ASSERT(__result->ai_addrlen == sizeof(::sockaddr_in6));
        ::sockaddr_in6 __native_endpoint;
        std::memcpy(&__native_endpoint, __result->ai_addr, sizeof(__native_endpoint));
        net::ip::address_v6 __addr{
          std::bit_cast<net::ip::address_v6::bytes_type>(__native_endpoint.sin6_addr)};
        __endpoint_ = net::ip::endpoint{__addr, __native_endpoint.sin6_port};
      }
    }

    operator net::ip::endpoint () const noexcept {
      return __endpoint_;
    }

    const net::ip::endpoint& endpoint() const noexcept {
      return __endpoint_;
    }

    std::string_view host_name() const noexcept {
      return __host_name_;
    }

    std::string_view service_name() const noexcept {
      return __service_name_;
    }

   private:
    std::string __host_name_;
    std::string __service_name_;
    net::ip::endpoint __endpoint_{};
  };
}

namespace exec::async {
  namespace __resolve {
    template <class _Scheduler, class _Receiver>
    struct __operation {
      struct __t;
    };

    template <class _Scheduler, class _Receiver>
    struct __next_receiver {
      struct __t {
        stdexec::__t<__operation<_Scheduler, _Receiver>>* __op_{};

        template <std::same_as<stdexec::set_value_t> _SetValue, std::same_as<__t> _Self>
        friend void tag_invoke(_SetValue, _Self&& __self) noexcept {
          STDEXEC_ASSERT(__self.__op_->__result_iter_ != nullptr);
          __self.__op_->__result_iter_ = __self.__op_->__result_iter_->ai_next;
          if (__self.__op_->__result_iter_) {
            __self.__op_->__start_next();
          } else {
            ::freeaddrinfo(__self.__op_->__request_.ar_result);
            stdexec::set_value(static_cast<_Receiver&&>(__self.__op_->__receiver_));
          }
        }

        template <std::same_as<stdexec::set_stopped_t> _SetStopped, std::same_as<__t> _Self>
        friend void tag_invoke(_SetStopped, _Self&& __self) noexcept {
          STDEXEC_ASSERT(__self.__op_->__request_.ar_result);
          ::freeaddrinfo(__self.__op_->__request_.ar_result);
          auto token = stdexec::get_stop_token(stdexec::get_env(__self.__op_->__receiver_));
          if (token.stop_requested()) {
            stdexec::set_stopped(static_cast<_Receiver&&>(__self.__op_->__receiver_));
          } else {
            stdexec::set_value(static_cast<_Receiver&&>(__self.__op_->__receiver_));
          }
        }

        template <std::same_as<stdexec::set_error_t> _SetError, std::same_as<__t> _Self, class _Error>
        friend void tag_invoke(_SetError, _Self&& __self, _Error&& __error) noexcept {
          STDEXEC_ASSERT(__self.__op_->__request_.ar_result);
          ::freeaddrinfo(__self.__op_->__request_.ar_result);
          stdexec::set_error(
            static_cast<_Receiver&&>(__self.__op_->__receiver_), static_cast<_Error&&>(__error));
        }

        template <std::same_as<stdexec::get_env_t> _GetEnv, std::same_as<__t> _Self>
        friend stdexec::env_of_t<_Receiver> tag_invoke(_GetEnv, const _Self& __self) noexcept {
          return stdexec::get_env(__self.__op_->__receiver_);
        }
      };
    };

    template <class _Scheduler, class _Receiver>
    struct __operation<_Scheduler, _Receiver>::__t {
      using __id = __operation;

      using __just_result = decltype(stdexec::just(stdexec::__declval<net::ip::resolver_result>()));
      using __next_sender = exec::__next_sender_of_t<_Receiver&, __just_result>;
      using __next_rcvr_t = stdexec::__t<__next_receiver<_Scheduler, _Receiver>>;

      [[no_unique_address]] _Receiver __receiver_;
      _Scheduler __scheduler_;
      net::ip::resolver_query __query_;
      ::gaicb __request_{};
      ::addrinfo* __result_iter_{};

      std::optional<stdexec::connect_result_t<__next_sender, __next_rcvr_t>> __next_op_{};
      ::gaicb* __requests_[1]{&this->__request_};
      sigevent_t __sigev{};

      explicit __t(
        _Scheduler&& __scheduler,
        net::ip::resolver_query __query,
        _Receiver&& __receiver)
        : __receiver_{static_cast<_Receiver&&>(__receiver)}
        , __scheduler_{static_cast<_Scheduler&&>(__scheduler)}
        , __query_{static_cast<net::ip::resolver_query&&>(__query)} {
        this->__request_.ar_name = this->__query_.host_name().c_str();
        this->__request_.ar_service = this->__query_.service_name().c_str();
        this->__request_.ar_request = &this->__query_.hints();
        this->__request_.ar_result = nullptr;
        this->__sigev.sigev_notify = SIGEV_THREAD;
        this->__sigev.sigev_value.sival_ptr = this;
        this->__sigev.sigev_notify_function = &__t::__notify;
      }

      void __start_next() noexcept {
        auto& __next_op = __next_op_.emplace(stdexec::__conv{[&] {
          auto __res = //
            stdexec::just(net::ip::resolver_result{
              this->__result_iter_, this->__query_.host_name(), this->__query_.service_name()});
          return stdexec::connect(exec::set_next(this->__receiver_, __res), __next_rcvr_t{this});
        }});
        stdexec::start(__next_op);
      }

      static void __notify(sigval __sigval) noexcept {
        auto& __self = *static_cast<__t*>(__sigval.sival_ptr);
        int rc = ::gai_error(__self.__requests_[0]);
        if (rc == 0) {
          if (__self.__request_.ar_result) {
            __self.__result_iter_ = __self.__request_.ar_result;
            __self.__start_next();
          } else {
            stdexec::set_value(static_cast<_Receiver&&>(__self.__receiver_));
          }
        } else if (rc == EAI_CANCELED) {
          stdexec::set_stopped(static_cast<_Receiver&&>(__self.__receiver_));
        } else if (rc != EAI_INPROGRESS) {
          stdexec::set_error(
            static_cast<_Receiver&&>(__self.__receiver_),
            std::error_code{rc, net::ip::resolver_error_category()});
        }
      }

      friend void tag_invoke(stdexec::start_t, __t& __self) noexcept {
        ::getaddrinfo_a(GAI_NOWAIT, __self.__requests_, 1, &__self.__sigev);
      }
    };

    template <class _Scheduler, class _Env>
    using __make_completion_signatures = stdexec::__try_make_completion_signatures<
      decltype(stdexec::on(
        stdexec::__declval<_Scheduler>(),
        stdexec::just(stdexec::__declval<net::ip::resolver_result>()))),
      _Env,
      stdexec::completion_signatures<
        stdexec::set_value_t(net::ip::resolver_result),
        stdexec::set_error_t(std::error_code),
        stdexec::set_stopped_t()>,
      stdexec::__mconst<stdexec::completion_signatures<>>>;

    template <class _Scheduler>
    struct __sender {
      struct __t {
        using __id = __sender;
        using is_sequence_sender = void;

        _Scheduler __scheduler_;
        net::ip::resolver_query __query_;

        template <
          std::same_as<sequence_connect_t> _SequenceConnect,
          stdexec::__decays_to<__t> _Self,
          class _Receiver>
        friend stdexec::__t<__operation<_Scheduler, _Receiver>>
          tag_invoke(_SequenceConnect, _Self&& __self, _Receiver&& __receiver) {
          return stdexec::__t<__operation<_Scheduler, _Receiver>>{
            static_cast<_Scheduler&&>(__self.__scheduler_),
            static_cast<net::ip::resolver_query&&>(__self.__query_),
            static_cast<_Receiver&&>(__receiver)};
        }

        template <stdexec::__decays_to<__t> _Self, class _Env>
        friend auto tag_invoke(stdexec::get_completion_signatures_t, _Self&&, const _Env&)
          -> __make_completion_signatures<_Scheduler, _Env>;
      };
    };

  }

  struct resolve_t {
    template <class _Resolver>
      requires stdexec::tag_invocable<resolve_t, _Resolver, const net::ip::resolver_query&>
    auto operator()(_Resolver&& __resolver, net::ip::resolver_query __query) const
      noexcept(stdexec::nothrow_tag_invocable<resolve_t, _Resolver, const net::ip::resolver_query&>)
        -> stdexec::tag_invoke_result_t<resolve_t, _Resolver, const net::ip::resolver_query&> {
      return tag_invoke(*this, static_cast<_Resolver&&>(__resolver), __query);
    }

    template <stdexec::scheduler _Scheduler>
      requires(!stdexec::tag_invocable<resolve_t, _Scheduler, const net::ip::resolver_query&>)
    auto operator()(_Scheduler __scheduler, net::ip::resolver_query __query) const
      -> stdexec::__t<__resolve::__sender<_Scheduler>> {
      return stdexec::__t<__resolve::__sender<_Scheduler>>{
        __scheduler, static_cast<net::ip::resolver_query&&>(__query)};
    }

    template <stdexec::scheduler _Scheduler, class _Arg, class... _Args>
      requires(!stdexec::__decays_to<_Arg, net::ip::resolver_query>)
           && stdexec::__callable<resolve_t, _Scheduler, net::ip::resolver_query>
           && std::constructible_from<net::ip::resolver_query, _Arg, _Args...>
    auto operator()(_Scheduler __scheduler, _Arg&& __arg, _Args&&... __args) const {
      return this->operator()(
        __scheduler,
        net::ip::resolver_query{static_cast<_Arg&&>(__arg), static_cast<_Args&&>(__args)...});
    }

    template <class _Arg, class... _Args>
      requires (!stdexec::scheduler<_Arg>) && std::constructible_from<net::ip::resolver_query, _Arg, _Args...>
    auto operator()(_Arg&& __arg, _Args&&... __args) const {
      return this->operator()(
        inline_scheduler(),
        net::ip::resolver_query{static_cast<_Arg&&>(__arg), static_cast<_Args&&>(__args)...});
    }
  };

  inline constexpr resolve_t resolve;
}