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

#include "stdexec/__detail/__meta.hpp"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./address.hpp"
#include "./endpoint.hpp"
#include "../concepts.hpp"
#include "../assert.hpp"
#include "../net_concepts.hpp"
#include "../sequence/sequence_concepts.hpp"

#include <netdb.h>

#include <csignal>
#include <cstring>
#include <string>
#include <string_view>

#include <exec/sequence_senders.hpp>
#include <exec/inline_scheduler.hpp>

namespace sio::ip {
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
  struct is_error_code_enum<sio::ip::gaierrc> : true_type { };
}

namespace sio::ip {
  struct resolver_error_category_t : std::error_category {
    const char* name() const noexcept override {
      return "resolver";
    }

    std::string message(int ec) const override {
      return ::gai_strerror(ec);
    }
  };

  static const resolver_error_category_t& resolver_error_category() noexcept {
    static const resolver_error_category_t impl{};
    return impl;
  }

  enum class resolver_flags {
    canonical_name = AI_CANONNAME,
    passive = AI_PASSIVE,
    numeric_host = AI_NUMERICHOST,
    numeric_service = AI_NUMERICSERV,
    v4_mapped = AI_V4MAPPED,
    all_matching = AI_ALL,
    address_configured = AI_ADDRCONFIG
  };

  constexpr resolver_flags operator|(resolver_flags lhs, resolver_flags rhs) noexcept {
    return static_cast<resolver_flags>(static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
  }

  constexpr resolver_flags operator&(resolver_flags lhs, resolver_flags rhs) noexcept {
    return static_cast<resolver_flags>(static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs));
  }

  constexpr resolver_flags operator^(resolver_flags lhs, resolver_flags rhs) noexcept {
    return static_cast<resolver_flags>(static_cast<unsigned>(lhs) ^ static_cast<unsigned>(rhs));
  }

  constexpr resolver_flags operator~(resolver_flags flags) noexcept {
    return static_cast<resolver_flags>(~static_cast<unsigned>(flags));
  }

  using addrinfo_type = ::addrinfo;

  class resolver_query {
   public:
    explicit resolver_query(
      const std::string& service,
      resolver_flags flags = resolver_flags::passive | resolver_flags::address_configured)
      : host_name_{}
      , service_name_{service} {
      hints_.ai_family = AF_UNSPEC;
      hints_.ai_socktype = 0;
      hints_.ai_protocol = 0;
      hints_.ai_flags = static_cast<int>(flags);
    }

    explicit resolver_query(
      const std::string& host_name,
      const std::string& service,
      resolver_flags flags = resolver_flags::address_configured)
      : host_name_{host_name}
      , service_name_{service} {
      hints_.ai_family = AF_UNSPEC;
      hints_.ai_socktype = 0;
      hints_.ai_protocol = 0;
      hints_.ai_flags = static_cast<int>(flags);
    }

    template <internet_protocol InternetProtocol>
    explicit resolver_query(
      InternetProtocol protocol,
      const std::string& service,
      resolver_flags flags = resolver_flags::passive | resolver_flags::address_configured)
      : host_name_{}
      , service_name_{service} {
      hints_.ai_family = protocol.family();
      hints_.ai_socktype = protocol.type();
      hints_.ai_protocol = protocol.protocol();
      hints_.ai_flags = static_cast<int>(flags);
    }

    template <internet_protocol InternetProtocol>
    explicit resolver_query(
      InternetProtocol protocol,
      const std::string& __host,
      const std::string& service,
      resolver_flags flags = resolver_flags::address_configured)
      : host_name_{__host}
      , service_name_{service} {
      hints_.ai_family = protocol.family();
      hints_.ai_socktype = protocol.type();
      hints_.ai_protocol = protocol.protocol();
      hints_.ai_flags = static_cast<int>(flags);
    }

    const addrinfo_type& hints() const noexcept {
      return hints_;
    }

    const std::string& host_name() const {
      return host_name_;
    }

    const std::string& service_name() const {
      return service_name_;
    }

   private:
    addrinfo_type hints_{};
    std::string host_name_;
    std::string service_name_;
  };

  class resolver_result {
   public:
    explicit resolver_result(
      addrinfo_type* result,
      std::string host_name,
      std::string service_name) noexcept
      : host_name_{static_cast<std::string&&>(host_name)}
      , service_name_{static_cast<std::string>(service_name)} {
      if (result->ai_addr->sa_family == AF_INET) {
        ::sockaddr_in native_endpoint = std::bit_cast<::sockaddr_in>(*result->ai_addr);
        ip::address_v4 addr{std::bit_cast<ip::address_v4::bytes_type>(native_endpoint.sin_addr)};
        endpoint_ = ip::endpoint{addr, native_endpoint.sin_port};
      } else {
        SIO_ASSERT(result->ai_addr->sa_family == AF_INET6);
        SIO_ASSERT(result->ai_addrlen == sizeof(::sockaddr_in6));
        ::sockaddr_in6 native_endpoint;
        std::memcpy(&native_endpoint, result->ai_addr, sizeof(native_endpoint));
        ip::address_v6 addr{std::bit_cast<ip::address_v6::bytes_type>(native_endpoint.sin6_addr)};
        endpoint_ = ip::endpoint{addr, native_endpoint.sin6_port};
      }
    }

    operator ip::endpoint() const noexcept {
      return endpoint_;
    }

    const ip::endpoint& endpoint() const noexcept {
      return endpoint_;
    }

    std::string_view host_name() const noexcept {
      return host_name_;
    }

    std::string_view service_name() const noexcept {
      return service_name_;
    }

   private:
    std::string host_name_;
    std::string service_name_;
    ip::endpoint endpoint_{};
  };
}

namespace sio::async {
  namespace resolve_ {
    template <class Scheduler, class Receiver>
    struct operation;

    template <class Scheduler, class Receiver>
    struct next_receiver {
      using receiver_concept = stdexec::receiver_t;
      operation<Scheduler, Receiver>* op_{};

      void set_value() && noexcept {
        SIO_ASSERT(op_->result_iter_ != nullptr);
        op_->result_iter_ = op_->result_iter_->ai_next;
        if (op_->result_iter_) {
          op_->start_next();
        } else {
          ::freeaddrinfo(op_->request_.ar_result);
          stdexec::set_value(static_cast<Receiver&&>(op_->receiver_));
        }
      }

      void set_stopped() && noexcept {
        SIO_ASSERT(op_->request_.ar_result);
        ::freeaddrinfo(op_->request_.ar_result);
        exec::set_value_unless_stopped(static_cast<Receiver&&>(op_->receiver_));
      }

      auto get_env() const noexcept -> stdexec::env_of_t<Receiver> {
        return stdexec::get_env(op_->receiver_);
      }
    };

    template <class Scheduler, class Receiver>
    struct operation {

      using just_result = decltype(stdexec::just(stdexec::__declval<ip::resolver_result>()));
      using next_sender = exec::next_sender_of_t<Receiver&, just_result>;
      using next_rcvr_t = next_receiver<Scheduler, Receiver>;

      [[no_unique_address]] Receiver receiver_;
      Scheduler scheduler_;
      ip::resolver_query query_;
      ::gaicb request_{};
      ::addrinfo* result_iter_{};

      std::optional<stdexec::connect_result_t<next_sender, next_rcvr_t>> next_op_{};
      ::gaicb* requests_[1]{&request_};
      sigevent_t sigev{};

      explicit operation(Scheduler&& scheduler, ip::resolver_query query, Receiver&& receiver)
        : receiver_{static_cast<Receiver&&>(receiver)}
        , scheduler_{static_cast<Scheduler&&>(scheduler)}
        , query_{static_cast<ip::resolver_query&&>(query)} {
        request_.ar_name = query_.host_name().c_str();
        request_.ar_service = query_.service_name().c_str();
        request_.ar_request = &query_.hints();
        request_.ar_result = nullptr;
        sigev.sigev_notify = SIGEV_THREAD;
        sigev.sigev_value.sival_ptr = this;
        sigev.sigev_notify_function = &operation::notify;
      }

      void start_next() noexcept try {
        auto& next_op = next_op_.emplace(stdexec::__emplace_from{[&] {
          auto res = //
            stdexec::just(
              ip::resolver_result{result_iter_, query_.host_name(), query_.service_name()});
          return stdexec::connect(exec::set_next(receiver_, std::move(res)), next_rcvr_t{this});
        }});
        stdexec::start(next_op);
      } catch (...) {

        stdexec::set_error(static_cast<Receiver&&>(receiver_), std::current_exception());
      }

      static void notify(sigval __sigval) noexcept {
        operation& self = *static_cast<operation*>(__sigval.sival_ptr);
        int rc = ::gai_error(self.requests_[0]);
        if (rc == 0) {
          if (self.request_.ar_result) {
            self.result_iter_ = self.request_.ar_result;
            self.start_next();
          } else {
            stdexec::set_value(static_cast<Receiver&&>(self.receiver_));
          }
        } else if (rc == EAI_CANCELED) {
          stdexec::set_stopped(static_cast<Receiver&&>(self.receiver_));
        } else if (rc != EAI_INPROGRESS) {
          stdexec::set_error(
            static_cast<Receiver&&>(self.receiver_),
            std::error_code{rc, ip::resolver_error_category()});
        }
      }

      void start() noexcept {
        ::getaddrinfo_a(GAI_NOWAIT, requests_, 1, &sigev);
      }
    };

    template <class Scheduler>
    struct sender {
      using sender_concept = exec::sequence_sender_t;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::error_code),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()>;

      using item_types =
        exec::item_types<decltype(stdexec::just(std::declval<ip::resolver_result>()))>;

      Scheduler scheduler_;
      ip::resolver_query query_;

      template <decays_to<sender> Self, class Receiver>
      friend operation<Scheduler, Receiver>
        tag_invoke(exec::subscribe_t, Self&& self, Receiver receiver) {
        return operation<Scheduler, Receiver>{
          static_cast<Scheduler&&>(self.scheduler_),
          static_cast<ip::resolver_query&&>(self.query_),
          static_cast<Receiver&&>(receiver)};
      }
    };

  } // namespace resolve_

  struct resolve_t {
    template <class _Resolver>
      requires stdexec::tag_invocable<resolve_t, _Resolver, const ip::resolver_query&>
    auto operator()(_Resolver&& __resolver, ip::resolver_query query) const
      noexcept(stdexec::nothrow_tag_invocable<resolve_t, _Resolver, const ip::resolver_query&>)
        -> stdexec::tag_invoke_result_t<resolve_t, _Resolver, const ip::resolver_query&> {
      return tag_invoke(*this, static_cast<_Resolver&&>(__resolver), query);
    }

    template <stdexec::scheduler Scheduler>
      requires(!stdexec::tag_invocable<resolve_t, Scheduler, const ip::resolver_query&>)
    resolve_::sender<Scheduler> operator()(Scheduler scheduler, ip::resolver_query query) const {
      return resolve_::sender<Scheduler>{scheduler, static_cast<ip::resolver_query&&>(query)};
    }

    template <stdexec::scheduler Scheduler, class _Arg, class... _Args>
      requires(!stdexec::__decays_to<_Arg, ip::resolver_query>)
           && callable<resolve_t, Scheduler, ip::resolver_query>
           && std::constructible_from<ip::resolver_query, _Arg, _Args...>
    auto operator()(Scheduler scheduler, _Arg&& __arg, _Args&&... __args) const {
      return this->operator()(
        scheduler, ip::resolver_query{static_cast<_Arg&&>(__arg), static_cast<_Args&&>(__args)...});
    }

    template <class _Arg, class... _Args>
      requires(!stdexec::scheduler<_Arg>)
           && std::constructible_from<ip::resolver_query, _Arg, _Args...>
    auto operator()(_Arg&& __arg, _Args&&... __args) const {
      return this->operator()(
        exec::inline_scheduler(),
        ip::resolver_query{static_cast<_Arg&&>(__arg), static_cast<_Args&&>(__args)...});
    }
  };

  inline constexpr resolve_t resolve;
}
