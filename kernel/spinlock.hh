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
    bool holding();

  private:
    uint locked; // Is the lock held?

    // For debugging:
    const char *name; // Name of lock.
    struct cpu *cpu;  // The cpu holding the lock.
};

} // namespace xv6
