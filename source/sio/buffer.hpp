#pragma once

#include "./const_buffer.hpp"
#include "./mutable_buffer.hpp"

#include <string>

namespace sio {

  template <class T, std::size_t N>
  const_buffer buffer(const T (&buffer)[N]) {
    return const_buffer(buffer, sizeof(T) * N);
  }

  template <class T, std::size_t N>
  mutable_buffer buffer(T (&buffer)[N]) {
    return mutable_buffer(buffer, sizeof(T) * N);
  }

  template <typename CharT, typename Traits, typename Alloc>
  const_buffer buffer(const std::basic_string<CharT, Traits, Alloc>& data) {
    return const_buffer(data.data(), data.size() * sizeof(CharT));
  }

  template <typename CharT, typename Traits, typename Alloc>
  mutable_buffer buffer(std::basic_string<CharT, Traits, Alloc>& data) {
    return mutable_buffer(data.data(), data.size() * sizeof(CharT));
  }

  template <typename CharT, typename Traits>
  const_buffer buffer(std::basic_string_view<CharT, Traits > data) {
    return const_buffer(data.data(), data.size() * sizeof(CharT));
  }

  template <class T, std::size_t N>
  mutable_buffer buffer(std::array<T, N>& data) {
    return mutable_buffer(data.data(), data.size() * N);
  }

  template <class T, std::size_t N>
  const_buffer buffer(std::array<const T, N>& data) {
    return const_buffer(data.data(), data.size() * N);
  }

  template <class T, std::size_t N>
  const_buffer buffer(const std::array<T, N>& data) {
    return const_buffer(data.data(), data.size() * N);
  }
}
