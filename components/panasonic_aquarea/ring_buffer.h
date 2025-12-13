#pragma once

#include <array>
#include <cstddef>
#include <algorithm>

namespace esphome {
namespace panasonic_aquarea {
template<typename T, size_t Capacity> class RingBuffer {
 private:
  std::array<T, Capacity> buffer_;
  size_t head_{0};  // Position to write next element
  size_t tail_{0};  // Position to read next element

 public:
  RingBuffer() = default;

  // Returns current number of elements in the buffer
  size_t size() const {
    if (head_ >= tail_)
      return head_ - tail_;
    else
      return Capacity - tail_ + head_;
  }

  // Push multiple elements from array
  // Caller must ensure buffer has enough space
  void push(const T *data, size_t count) {
    for (size_t i = 0; i < count; i++) {
      buffer_[head_] = data[i];
      head_ = (head_ + 1) % Capacity;
    }
  }

  // Peek at element at given offset from tail (without removing)
  // offset=0 returns first element, offset=1 second element, etc.
  // Caller must ensure offset < size()
  const T &peek(size_t offset = 0) const {
    size_t pos = (tail_ + offset) % Capacity;
    return buffer_[pos];
  }

  // Pop single element from buffer
  // Caller must ensure buffer is not empty
  T pop() {
    T value = buffer_[tail_];
    tail_ = (tail_ + 1) % Capacity;
    return value;
  }

  // Remove count elements from front of buffer
  // Caller must ensure count <= size()
  void discard_until(const T &value) {
    while (size() > 0 && peek() != value)
      pop();
  }
};

// Type alias to hide buffer size implementation detail
using ResponseBuffer = RingBuffer<uint8_t, 512>;

}  // namespace panasonic_aquarea
}  // namespace esphome
