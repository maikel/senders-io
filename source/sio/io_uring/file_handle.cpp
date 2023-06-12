#include "./file_handle.hpp"

namespace sio::io_uring {

  write_submission::write_submission(
    std::variant<::iovec, std::span<::iovec>> buffers,
    int fd,
    ::off_t offset) noexcept
    : buffers_{buffers}
    , fd_{fd}
    , offset_{offset} {
  }

  write_submission::~write_submission() = default;

  void write_submission::submit(::io_uring_sqe& sqe) const noexcept {
    ::io_uring_sqe sqe_{};
    sqe_.opcode = IORING_OP_WRITEV;
    sqe_.fd = fd_;
    sqe_.off = offset_;
    if (buffers_.index() == 0) {
      sqe_.addr = std::bit_cast<__u64>(std::get_if<0>(&buffers_));
      sqe_.len = 1;
    } else {
      std::span<const ::iovec> buffers = *std::get_if<1>(&buffers_);
      sqe_.addr = std::bit_cast<__u64>(buffers.data());
      sqe_.len = buffers.size();
    }
    sqe = sqe_;
  }

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