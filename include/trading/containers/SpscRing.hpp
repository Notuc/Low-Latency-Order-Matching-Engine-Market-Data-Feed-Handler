#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>
#include <stdexcept>

namespace trading::containers {

// Single-Producer / Single-Consumer ring. Capacity must be a power of two.
template <class T>
class SpscRing {
  static_assert(std::is_move_constructible_v<T>, "T must be movable");
public:
  explicit SpscRing(std::size_t capacity_pow2)
  : mask_(capacity_pow2 - 1), buf_(capacity_pow2) {
    if ((capacity_pow2 & mask_) != 0) throw std::runtime_error("capacity must be power of two");
  }

  // Producer — rvalue push
  bool try_push(T&& v) noexcept {
    auto h = head_.load(std::memory_order_relaxed);
    auto next = h + 1;
    if (next - tail_.load(std::memory_order_acquire) > buf_.size()) return false; // full
    buf_[h & mask_] = std::move(v);
    head_.store(next, std::memory_order_release);
    return true;
  }

  // Producer — lvalue push (copy)
  bool try_push(const T& v) noexcept {
    auto h = head_.load(std::memory_order_relaxed);
    auto next = h + 1;
    if (next - tail_.load(std::memory_order_acquire) > buf_.size()) return false; // full
    buf_[h & mask_] = v;
    head_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer
  bool try_pop(T& out) noexcept {
    auto t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false; // empty
    out = std::move(buf_[t & mask_]);
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }

  std::size_t size() const noexcept {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }

private:
  const std::size_t        mask_;
  std::vector<T>           buf_;
  std::atomic<std::size_t> head_{0}; // producer writes
  std::atomic<std::size_t> tail_{0}; // consumer writes
};

} 

