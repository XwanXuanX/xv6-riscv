// Per-process kernel stacks are mapped in the shared kernel page table.
// Each stack uses a two-page virtual slot below TRAMPOLINE:
//   [guard page: unmapped] [stack page: mapped read/write]
// The guard page catches overflow toward higher addresses (toward TRAMPOLINE).
//
// MAX_VA
//   ┌──────────────────────────┐
//   │ trampoline (1 page)      │  TRAMPOLINE
//   ├──────────────────────────┤
//   │ kstack slot 0: guard     │  unmapped
//   ├──────────────────────────┤
//   │ kstack slot 0: stack     │  mapped per process
//   ├──────────────────────────┤
//   │ kstack slot 1: guard     │
//   ├──────────────────────────┤
//   │ kstack slot 1: stack     │
//   ├──────────────────────────┤
//   │ ... (grows downward)     │
//   └──────────────────────────┘
//   KSTACK_VA_FLOOR
#pragma once

#include "kernel/lib/types.h"
#include "kernel/sync/spinlock.h"
#include "kernel/util/singletony.h"

namespace xv6 {

namespace test {
void kstack_self_test();
}

// True if va is a valid stack-page KVA from this allocator (page-aligned slot).
[[nodiscard]] bool kstack_kva_valid(uint64 va);

// Allocates kernel virtual addresses for per-process kernel stack pages.
// Does not allocate physical pages or modify page tables.
class kstack_allocator : public util::singleton<kstack_allocator> {
    friend class singleton;

  public:
    void init();

    // Pop a recycled stack KVA, or bump-allocate a new slot. nullptr if full.
    void *alloc();

    // Return a stack KVA to the freelist.
    void free(void *va);

  private:
    struct free_entry {
        free_entry *next;
        uint64 stack_kva;
    };

    kstack_allocator() = default;

    [[nodiscard]] void *alloc_locked();
    void free_locked(void *va);

    mutable spinlock lock_{};
    bool lock_inited_ = false;
    free_entry *freelist_ = nullptr;
    // Number of slots ever bump-allocated (slot indices [0, next_slot_)).
    uint64 next_slot_ = 0;
};

} // namespace xv6