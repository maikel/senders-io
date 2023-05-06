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

#include <stdexec/execution.hpp>

#include <exec/timed_scheduler.hpp>
#include "./async_resource.hpp"

#include <filesystem>

namespace sio { namespace async {
  enum class mode : unsigned char {
    unchanged = 0,
    none = 2,
    attr_read = 4,
    attr_write = 5,
    read = 6,
    write = 7,
    append = 9
  };

  enum class creation : unsigned char {
    open_existing = 0,
    only_if_existing,
    if_needed,
    truncate_existing,
    always_new
  };

  enum class caching : unsigned char {
    unchanged = 0,
    none = 1,
    only_metadata = 2,
    reads = 3,
    reads_and_metadata = 5,
    all = 6,
    safety_barriers = 7,
    temporary = 8
  };

  // namespace file {
  //   enum class flag : unsigned char {
  //     unlink_on_first_close = 0,
  //     disable_safety_barriers,
  //     disable_safety_unlinks,
  //     disable_prefetching,
  //     maximum_prefetching,
  //     win_disable_sparse_file_creation,
  //     win_create_case_sensitive_directory
  //   };
  // }

  template <class Handle>
  concept path_handle = stdexec::regular<Handle> && requires(Handle handle) {
    { handle.path() } -> std::convertible_to<std::filesystem::path>;
  };

  template <class Res, class Env = stdexec::empty_env>
  concept path_resource = resource<Res> && path_handle<resource_token_of_t<Res, Env>>;

  template <class Sender, class Tp, class Env = stdexec::no_env>
  concept single_value_sender =                    //
    stdexec::__single_typed_sender<Sender, Env> && //
    std::same_as<Tp, stdexec::__single_sender_value_t<Sender, Env>>;

  struct path_t {
    template <class Factory, class... Args>
      requires stdexec::tag_invocable<path_t, Factory, Args...>
    auto operator()(const Factory& factory, Args&&... args) const
      noexcept(stdexec::nothrow_tag_invocable<path_t, Factory, Args...>)
        -> stdexec::tag_invoke_result_t<path_t, Factory, Args...> {
      return tag_invoke(*this, factory, static_cast<Args&&>(args)...);
    }
  };

  template <class Factory>
  using path_factory = callable<path_t, Factory, std::filesystem::path>;

  template <path_factory Factory>
  using path_handle_of_t =
    resource_token_of_t<call_result_t<path_t, Factory, std::filesystem::path>>;

  namespace byte_stream_ {
    struct read_t {
      template <class Handle, class MutableBufferSequence>
        requires stdexec::tag_invocable<read_t, Handle, MutableBufferSequence>
      auto operator()(const Handle& handle, MutableBufferSequence&& buffers) const
        noexcept(stdexec::nothrow_tag_invocable<read_t, Handle, MutableBufferSequence>)
          -> stdexec::tag_invoke_result_t<read_t, Handle, MutableBufferSequence> {
        using __result_t = tag_invoke_result_t<read_t, Handle, MutableBufferSequence>;
        static_assert(
          single_value_sender<__result_t, MutableBufferSequence>,
          "read must return a sender that completes with the input buffers type");
        return tag_invoke(*this, handle, static_cast<MutableBufferSequence&&>(buffers));
      }
    };

    struct write_t {
      template <class Handle, class ConstBufferSequence>
        requires stdexec::tag_invocable<write_t, Handle, ConstBufferSequence>
      auto operator()(const Handle& handle, ConstBufferSequence&& buffers) const
        noexcept(stdexec::nothrow_tag_invocable<write_t, Handle, ConstBufferSequence>)
          -> stdexec::tag_invoke_result_t<write_t, Handle, ConstBufferSequence> {
        using __result_t = stdexec::tag_invoke_result_t<read_t, Handle, ConstBufferSequence>;
        static_assert(
          single_value_sender<__result_t, ConstBufferSequence>,
          "write must return a sender that completes with the input buffers type");
        return tag_invoke(*this, handle, static_cast<ConstBufferSequence&&>(buffers));
      }
    };
  } // namespace byte_stream_

  using byte_stream_::read_t;
  using byte_stream_::write_t;
  inline constexpr read_t read;
  inline constexpr write_t write;

  template <class Stream>
  concept with_buffer_typedefs = requires {
    typename Stream::buffer_type;
    typename Stream::const_buffer_type;
    typename Stream::buffers_type;
    typename Stream::const_buffers_type;
  };

  template <class Stream>
  concept with_offset = requires { typename Stream::offset_type; };

  template <with_offset Stream>
  using offset_type_of_t = typename Stream::offset_type;

  template <with_buffer_typedefs Stream>
  using buffer_type_of_t = typename Stream::buffer_type;

  template <with_buffer_typedefs Stream>
  using buffers_type_of_t = typename Stream::buffers_type;

  template <with_buffer_typedefs Stream>
  using const_buffer_type_of_t = typename Stream::const_buffer_type;

  template <with_buffer_typedefs Stream>
  using const_buffers_type_of_t = typename Stream::const_buffers_type;

  template <class ByteStream>
  concept readable_byte_stream =
    with_buffer_typedefs<ByteStream>
    && requires(ByteStream stream, buffers_type_of_t<ByteStream> buffers) {
         { async::read(stream, buffers) } -> single_value_sender<buffers_type_of_t<ByteStream>>;
       };

  template <class ByteStream>
  concept writable_byte_stream =
    with_buffer_typedefs<ByteStream>
    && requires(ByteStream stream, const_buffers_type_of_t<ByteStream> const_buffers) {
         {
           async::write(stream, const_buffers)
         } -> single_value_sender<const_buffers_type_of_t<ByteStream>>;
       };

  template <class ByteStream>
  concept byte_stream =                 //
    with_buffer_typedefs<ByteStream> && //
    readable_byte_stream<ByteStream> && //
    writable_byte_stream<ByteStream>;

  template <class ByteStream>
  concept seekable_byte_stream = //
    byte_stream<ByteStream> &&   //
    with_offset<ByteStream> &&   //
    requires(
      ByteStream stream,
      buffers_type_of_t<ByteStream> buffers,
      const_buffers_type_of_t<ByteStream> const_buffers,
      offset_type_of_t<ByteStream> offset) {
      {
        async::read(stream, buffers, offset)
      } -> single_value_sender<buffers_type_of_t<ByteStream>>;
      {
        async::write(stream, const_buffers, offset)
      } -> single_value_sender<buffers_type_of_t<ByteStream>>;
    };

  template <class _FileHandle>
  concept file_handle = path_handle<_FileHandle> && seekable_byte_stream<_FileHandle>;

  template <class Res, class Env = stdexec::no_env>
  concept file_resource = resource<Res> && file_handle<resource_token_of_t<Res, Env>>;

  struct file_t { };

  inline constexpr file_t file;

  template <class Factory>
  concept file_factory =     //
    path_factory<Factory> && //
    requires(
      Factory factory,
      path_handle_of_t<Factory> base,
      std::filesystem::path path,
      mode mode,
      creation creation,
      caching caching) {
      { async::file(factory, path) } -> file_resource;
      { async::file(factory, base, path) } -> file_resource;
      { async::file(factory, base, path, mode) } -> file_resource;
      { async::file(factory, base, path, mode) } -> file_resource;
      { async::file(factory, base, path, mode, creation) } -> file_resource;
      { async::file(factory, base, path, mode, creation, caching) } -> file_resource;
    };

  template <class Scheduler>
  concept io_scheduler = exec::timed_scheduler<Scheduler> && file_factory<Scheduler>;

} // namespace async
} // namespace exec