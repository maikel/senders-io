/*
 * Copyright (c) 2024 Maikel Nadolski
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

#include "./async_resource.hpp"
#include "./concepts.hpp"

#include <exec/timed_scheduler.hpp>
#include <stdexec/__detail/__execution_fwd.hpp>

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

  // namespace open_file {
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

  template <class Res, class Env = stdexec::env<>>
  concept path_resource = resource<Res> && path_handle<resource_token_of_t<Res, Env>>;

  template <class Sender, class Tp, class Env = stdexec::env<>>
  concept single_value_sender =                    //
    stdexec::__single_value_sender<Sender, Env> && //
    std::same_as<Tp, stdexec::__single_sender_value_t<Sender, Env>>;

  namespace open_path_ {
    struct open_path_t;
  }

  extern const open_path_::open_path_t open_path;

  namespace open_path_ {
    template <class Factory, class... Args>
    concept has_path_member_customization = requires(Factory factory, Args&&... args) {
      factory.open_path(static_cast<Args&&>(args)...);
    };

    template <class Factory, class... Args>
    concept nothrow_path_member_customization = requires(Factory factory, Args&&... args) {
      { factory.open_path(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Factory, class... Args>
    concept has_path_static_member_customization = requires(Factory factory, Args&&... args) {
      Factory::open_path(factory, static_cast<Args&&>(args)...);
    };

    template <class Factory, class... Args>
    concept nothrow_path_static_member_customization = requires(Factory factory, Args&&... args) {
      { Factory::open_path(factory, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Factory, class... Args>
    concept has_path_customization = has_path_member_customization<Factory, Args...>
                                  || has_path_static_member_customization<Factory, Args...>;

    template <class Factory, class... Args>
    concept nothrow_path_customization = nothrow_path_member_customization<Factory, Args...>
                                      || nothrow_path_static_member_customization<Factory, Args...>;

    struct open_path_t {
      template <class Factory, class... Args>
        requires has_path_customization<Factory, Args...>
      auto operator()(const Factory& factory, Args&&... args) const
        noexcept(nothrow_path_customization<Factory, Args...>) {
        if constexpr (has_path_member_customization<Factory, Args...>) {
          return factory.open_path(static_cast<Args&&>(args)...);
        } else {
          return Factory::open_path(factory, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  using open_path_t = open_path_::open_path_t;
  inline constexpr open_path_t open_path{};

  template <class Factory>
  concept path_factory = callable<open_path_t, Factory, std::filesystem::path>;

  template <path_factory Factory>
  using path_handle_of_t =
    resource_token_of_t<call_result_t<open_path_t, Factory, std::filesystem::path>>;

  namespace byte_stream_ {
    struct read_some_t;
  }

  extern const byte_stream_::read_some_t read_some;

  namespace byte_stream_ {
    template <class Handle, class... Args>
    concept has_read_some_member_function = requires(const Handle& handle, Args&&... args) {
      handle.read_some(static_cast<Args&&>(args)...);
    };

    template <class Handle, class... Args>
    concept nothrow_read_some_member_function = requires(const Handle& handle, Args&&... args) {
      { handle.read_some(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Handle, class... Args>
    concept has_static_read_some_member_function = requires(const Handle& handle, Args&&... args) {
      Handle::read_some(handle, static_cast<Args&&>(args)...);
    };

    template <class Handle, class... Args>
    concept nothrow_static_read_some_member_function =
      requires(const Handle& handle, Args&&... args) {
        { Handle::read_some(handle, static_cast<Args&&>(args)...) } noexcept;
      };

    template <class Handle, class... Args>
    concept has_read_some_customization = has_read_some_member_function<Handle, Args...>
                                       || has_static_read_some_member_function<Handle, Args...>;

    template <class Handle, class... Args>
    concept nothrow_read_some_customization =
      nothrow_read_some_member_function<Handle, Args...>
      || nothrow_static_read_some_member_function<Handle, Args...>;

    struct read_some_t {
      template <class Handle, class... Args>
        requires has_read_some_customization<Handle, Args...>
      auto operator()(const Handle& handle, Args&&... args) const
        noexcept(nothrow_read_some_customization<Handle, Args...>) {
        if constexpr (has_read_some_member_function<Handle, Args...>) {
          return handle.read_some(static_cast<Args&&>(args)...);
        } else {
          return Handle::read_some(handle, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  namespace byte_stream_ {
    struct read_t;
  }

  extern const byte_stream_::read_t read;

  namespace byte_stream_ {
    template <class Handle, class... Args>
    concept has_read_member_function = requires(const Handle& handle, Args&&... args) {
      handle.read(static_cast<Args&&>(args)...);
    };

    template <class Handle, class... Args>
    concept nothrow_read_member_function = requires(const Handle& handle, Args&&... args) {
      { handle.read(static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Handle, class... Args>
    concept has_static_read_member_function = requires(const Handle& handle, Args&&... args) {
      Handle::read(handle, static_cast<Args&&>(args)...);
    };

    template <class Handle, class... Args>
    concept nothrow_static_read_member_function = requires(const Handle& handle, Args&&... args) {
      { Handle::read(handle, static_cast<Args&&>(args)...) } noexcept;
    };

    template <class Handle, class... Args>
    concept has_read_customization =
      has_read_member_function<Handle, Args...> || has_static_read_member_function<Handle, Args...>;

    template <class Handle, class... Args>
    concept nothrow_read_customization = nothrow_read_member_function<Handle, Args...>
                                      || nothrow_static_read_member_function<Handle, Args...>;

    struct read_t {
      template <class Handle, class... Args>
        requires has_read_customization<Handle, Args...>
      auto operator()(const Handle& handle, Args&&... args) const
        noexcept(nothrow_read_customization<Handle, Args...>) {
        if constexpr (has_read_member_function<Handle, Args...>) {
          return handle.read(static_cast<Args&&>(args)...);
        } else {
          return Handle::read(handle, static_cast<Args&&>(args)...);
        }
      }
    };
  }

  namespace byte_stream_ {
    struct write_some_t;
  }

  extern const byte_stream_::write_some_t write_some;

  namespace byte_stream_ {
    template <class Handle, class... Args>
    concept has_write_some_member_function = requires(const Handle& handle, Args... buffers) {
      handle.write_some(buffers...);
    };

    template <class Handle, class... Args>
    concept nothrow_write_some_member_function = requires(const Handle& handle, Args&&... buffers) {
      { handle.write_some(static_cast<Args&&>(buffers)...) } noexcept;
    };

    template <class Handle, class... Args>
    concept has_static_write_some_member_function =
      requires(const Handle& handle, Args&&... buffers) {
        Handle::write_some(handle, static_cast<Args&&>(buffers)...);
      };

    template <class Handle, class... Args>
    concept nothrow_static_write_some_member_function =
      requires(const Handle& handle, Args&&... buffers) {
        { Handle::write_some(handle, static_cast<Args&&>(buffers)...) } noexcept;
      };

    template <class Handle, class... Args>
    concept has_write_some_customization = has_write_some_member_function<Handle, Args...>
                                        || has_static_write_some_member_function<Handle, Args...>;

    template <class Handle, class... Args>
    concept nothrow_write_some_customization =
      nothrow_write_some_member_function<Handle, Args...>
      || nothrow_static_write_some_member_function<Handle, Args...>;

    struct write_some_t {
      template <class Handle, class... Args>
        requires has_write_some_customization<Handle, Args...>
      auto operator()(const Handle& handle, Args&&... buffers) const
        noexcept(nothrow_write_some_customization<Handle, Args...>) {
        if constexpr (has_write_some_member_function<Handle, Args...>) {
          return handle.write_some(static_cast<Args&&>(buffers)...);
        } else {
          return Handle::write_some(handle, static_cast<Args&&>(buffers)...);
        }
      }
    };
  } // namespace byte_stream_

  namespace byte_stream_ {
    struct write_t;
  }

  extern const byte_stream_::write_t write;

  namespace byte_stream_ {
    template <class Handle, class ConstBufferSequence>
    concept has_write_member_function =
      requires(const Handle& handle, ConstBufferSequence buffers) { handle.write(buffers); };

    template <class Handle, class ConstBufferSequence>
    concept nothrow_write_member_function =
      requires(const Handle& handle, ConstBufferSequence buffers) {
        { handle.write(buffers) } noexcept;
      };

    template <class Handle, class ConstBufferSequence>
    concept has_static_write_member_function =
      requires(const Handle& handle, ConstBufferSequence buffers) {
        Handle::write(handle, buffers);
      };

    template <class Handle, class ConstBufferSequence>
    concept nothrow_static_write_member_function =
      requires(const Handle& handle, ConstBufferSequence buffers) {
        { Handle::write(handle, buffers) } noexcept;
      };

    template <class Handle, class ConstBufferSequence>
    concept has_write_customization = has_write_member_function<Handle, ConstBufferSequence>
                                   || has_static_write_member_function<Handle, ConstBufferSequence>;

    template <class Handle, class ConstBufferSequence>
    concept nothrow_write_customization =
      nothrow_write_member_function<Handle, ConstBufferSequence>
      || nothrow_static_write_member_function<Handle, ConstBufferSequence>;

    struct write_t {
      template <class Handle, class ConstBufferSequence>
        requires has_write_customization<Handle, ConstBufferSequence>
      auto operator()(const Handle& handle, ConstBufferSequence&& buffers) const
        noexcept(nothrow_write_customization<Handle, ConstBufferSequence>) {
        if constexpr (has_write_member_function<Handle, ConstBufferSequence>) {
          return handle.write(static_cast<ConstBufferSequence&&>(buffers));
        } else {
          return Handle::write(handle, static_cast<ConstBufferSequence&&>(buffers));
        }
      }
    };
  } // namespace byte_stream_

  using byte_stream_::read_some_t;
  using byte_stream_::read_t;
  using byte_stream_::write_some_t;
  using byte_stream_::write_t;
  inline constexpr read_some_t read_some;
  inline constexpr read_t read;
  inline constexpr write_some_t write_some;
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
         { async::read_some(stream, buffers) } -> single_value_sender<std::size_t>;
       };

  template <class ByteStream>
  concept writable_byte_stream =
    with_buffer_typedefs<ByteStream>
    && requires(ByteStream stream, const_buffers_type_of_t<ByteStream> const_buffers) {
         { async::write_some(stream, const_buffers) } -> single_value_sender<std::size_t>;
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
      { async::read_some(stream, buffers, offset) } -> single_value_sender<std::size_t>;
      { async::write_some(stream, const_buffers, offset) } -> single_value_sender<std::size_t>;
    };

  template <class _FileHandle>
  concept file_handle = path_handle<_FileHandle> && seekable_byte_stream<_FileHandle>;

  template <class Res, class Env = stdexec::env<>>
  concept file_resource = resource<Res> && file_handle<resource_token_of_t<Res, Env>>;

  namespace open_file_ {
    struct open_file_t;
    extern const open_file_t open_file;

    template <class FileFactory, class... Args>
    concept has_member_customization = //
      requires(FileFactory&& factory, Args&&... args) {
        static_cast<FileFactory&&>(factory).open_file(static_cast<Args&&>(args)...);
      };

    template <class FileFactory, class... Args>
    concept nothrow_member_customization = //
      requires(FileFactory&& factory, Args&&... args) {
        { static_cast<FileFactory&&>(factory).open_file(static_cast<Args&&>(args)...) } noexcept;
      };

    template <class FileFactory, class... Args>
    concept has_static_member_customization = //
      requires(FileFactory&& factory, Args&&... args) {
        decay_t<FileFactory>::open_file(
          static_cast<FileFactory>(factory), static_cast<Args&&>(args)...);
      };

    template <class FileFactory, class... Args>
    concept nothrow_static_member_customization = //
      requires(FileFactory&& factory, Args&&... args) {
        {
          decay_t<FileFactory>::open_file(
            static_cast<FileFactory>(factory), static_cast<Args&&>(args)...)
        } noexcept;
      };

    template <class FileFactory, class... Args>
    concept has_customization =                         //
      has_member_customization<FileFactory, Args...> || //
      has_static_member_customization<FileFactory, Args...>;

    template <class FileFactory, class... Args>
    concept nothrow_customization =                         //
      nothrow_member_customization<FileFactory, Args...> || //
      nothrow_static_member_customization<FileFactory, Args...>;

    template <class FileFactory>
    concept has_full_customization = //
      path_factory<FileFactory> &&   //
      has_customization<
        FileFactory,
        std::filesystem::path,
        path_handle_of_t<FileFactory>,
        mode,
        creation,
        caching>;

    template <class FileFactory>
    concept nothrow_full_customization = //
      path_factory<FileFactory> &&       //
      nothrow_customization<
        FileFactory,
        std::filesystem::path,
        path_handle_of_t<FileFactory>,
        mode,
        creation,
        caching>;

    struct open_file_t {
      template <path_factory FileFactory>
        requires has_full_customization<FileFactory>
      auto operator()(
        FileFactory&& factory,
        std::filesystem::path path,
        path_handle_of_t<FileFactory> base,
        mode flags,
        creation creat = creation::open_existing,
        caching chache = caching::unchanged) const
        noexcept(nothrow_full_customization<FileFactory>) {
        if constexpr (
          has_member_customization<
            FileFactory,
            std::filesystem::path,
            path_handle_of_t<FileFactory>,
            mode,
            creation,
            caching>) {
          return static_cast<FileFactory&&>(factory).open_file(path, base, flags, creat, chache);
        } else {
          return FileFactory::open_file(
            static_cast<FileFactory&&>(factory), path, base, flags, creat, chache);
        }
      }

      template <path_factory FileFactory>
        requires has_full_customization<FileFactory> && requires {
          path_handle_of_t<FileFactory>::current_directory();
        }
      auto operator()(
        FileFactory&& factory,
        std::filesystem::path path,
        mode flags,
        creation creat = creation::open_existing,
        caching cache = caching::unchanged) const
        noexcept(nothrow_full_customization<FileFactory>) {
        return this->operator()(
          static_cast<FileFactory&&>(factory),
          static_cast<std::filesystem::path&&>(path),
          path_handle_of_t<FileFactory>::current_directory(),
          flags,
          creat,
          cache);
      }
    };

    inline constexpr open_file_t open_file{};
  }

  using open_file_::open_file_t;
  using open_file_::open_file;

  template <class Factory>
  concept file_factory =     //
    path_factory<Factory> && //
    requires(
      Factory factory,
      std::filesystem::path path,
      path_handle_of_t<Factory> base,
      mode mode,
      creation creation,
      caching caching) {
      { async::open_file(factory, base, path, mode, creation, caching) } -> file_resource;
    };

  template <class Scheduler>
  concept io_scheduler = exec::timed_scheduler<Scheduler> && file_factory<Scheduler>;

} // namespace async
} // namespace sio
