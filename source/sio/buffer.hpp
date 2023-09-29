#pragma once

#include "./const_buffer.hpp"
#include "./mutable_buffer.hpp"

namespace sio {

  template <class T, std::size_t N>
  const_buffer buffer(const T (&buffer)[N]) {
    return const_buffer(buffer, sizeof(T) * N);
  }

  template <class T, std::size_t N>
  mutable_buffer buffer(T (&buffer)[N]) {
    return mutable_buffer(buffer, sizeof(T) * N);
  }
}