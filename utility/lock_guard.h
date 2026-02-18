// Self implementation of std::lock_guard with RAII style lock management
#pragma once

namespace xv6::util {

template <typename Mutex> class lock_guard {
  public:
    explicit lock_guard(Mutex &m) : m_(m) { m_.lock(); }

    ~lock_guard() { m_.unlock(); }

  private:
    Mutex &m_;
};

} // namespace xv6::util