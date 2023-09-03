#pragma once

#include "./io_concepts.hpp"
#include "./sequence/fork.hpp"
#include "./sequence/ignore_all.hpp"
#include "./sequence/iterate.hpp"
#include "./sequence/let_value_each.hpp"
#include "./sequence/zip.hpp"

#include <span>

namespace sio::async {
  template <seekable_byte_stream ByteStream>
  auto read_batched(
    ByteStream stream,
    buffers_type_of_t<ByteStream> buffers,
    std::span<offset_type_of_t<ByteStream>> offsets) {
    return                                    //
      zip(iterate(buffers), iterate(offsets)) //
      | fork()                                //
      | let_value_each(
        [stream](buffer_type_of_t<ByteStream> buffer, offset_type_of_t<ByteStream> offset) {
          return async::read(stream, buffer, offset);
        })
      | ignore_all();
  }
}