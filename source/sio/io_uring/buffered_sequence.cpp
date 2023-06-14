#include "./buffered_sequence.hpp"

namespace sio {
  std::size_t advance_buffers(std::variant<::iovec, std::span<::iovec>>& buffers, std::size_t n) noexcept {
    if (::iovec* buffer = std::get_if<0>(&buffers)) {
      STDEXEC_ASSERT(n <= buffer->iov_len);
      buffer->iov_base = static_cast<char*>(buffer->iov_base) + n;
      buffer->iov_len -= n;
    } else {
      std::span<::iovec>* buffers_ = std::get_if<1>(&buffers);
      STDEXEC_ASSERT(buffers_);
      while (n && !buffers_->empty()) {
        if (n >= buffers_->front().iov_len) {
          n -= buffers_->front().iov_len;
          *buffers_ = buffers_->subspan(1);
        } else {
          STDEXEC_ASSERT(n < buffers_->front().iov_len);
          buffers_->front().iov_base = static_cast<char*>(buffers_->front().iov_base) + n;
          buffers_->front().iov_len -= n;
          n = 0;
        }
      }
    }
    return n;
  }
}