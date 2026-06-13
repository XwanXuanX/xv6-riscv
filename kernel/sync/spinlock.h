#pragma once

#include "kernel/lib/types.h"

namespace xv6 {

// Mutual exclusion lock.
class spinlock {
  public:
    spinlock() = default;

    // Initialize the lock
    void init_lock(const char *name);

    // Acquire the lock
    void lock();

    // Release the lock
    void unlock();

    // Is anyone holding the lock
    [[nodiscard]] bool holding() const;

  private:
    uint locked_; // Is the lock held?

    // For debugging:
    const char *name_; // Name of lock.
    struct cpu *cpu_;  // The cpu holding the lock.
};

} // namespace xv6
