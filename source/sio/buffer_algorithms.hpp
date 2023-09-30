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
#include <cstddef>

namespace sio {
  template <class BufferSequence>
  auto without_prefix(BufferSequence&& sequence, std::size_t prefix_size) {
    std::size_t size = sequence.buffer_size();
    std::size_t rest = prefix_size <= size ? size - prefix_size : 0;
    return sequence.suffix(rest);
  }
}