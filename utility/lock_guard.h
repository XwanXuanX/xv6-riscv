// Self implementation of std::lock_guard with RAII style lock management
#pragma once

#include <concepts>

namespace xv6::util {

template <typename T>
concept lockable = requires(T m) {
    { m.lock() } -> std::same_as<void>;
    { m.unlock() } -> std::same_as<void>;
};

template <lockable Mutex> class lock_guard {
  public:
    explicit lock_guard(Mutex &m) : m_(m) { m_.lock(); }

    ~lock_guard() { m_.unlock(); }

    // prevent accidental copying that leads to double unlock bugs
    lock_guard(const lock_guard &) = delete;
    lock_guard &operator=(const lock_guard &) = delete;

  private:
    Mutex &m_;
};

} // namespace xv6::util