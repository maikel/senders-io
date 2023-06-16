#pragma once

namespace sio {

struct request_stop_t {
  template <class Operation>
    requires requires (Operation& op) { 
      { op.request_stop() } noexcept;
    }
  auto operator()(Operation& op) const noexcept {
    return op.request_stop();
  }
};

inline constexpr request_stop_t request_stop{};

}