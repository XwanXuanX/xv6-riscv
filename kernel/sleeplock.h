#pragma once
#include "kernel/spinlock.h"

namespace xv6 {

// Long-term locks for processes
class sleeplock {
  public:
    sleeplock() = default;

    // Initialize the lock
    void init_lock();

    // Acquire the lock
    void lock();

    // Release the lock
    void unlock();

    // Is anyone holding the lock
    [[nodiscard]] bool holding();

  private:
    uint locked_; // Is the lock held?
    spinlock lk_; // spinlock protecting this sleep lock

    // For debugging:
    const char *name_; // Name of lock.
    int pid_;          // Process holding lock
};

} // namespace xv6