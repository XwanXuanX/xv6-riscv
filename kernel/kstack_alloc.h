// User process's kernel stack is mapped to some virtual addresses in the
// kernel's address space. This allocator provides a number of available kernel
// VA that the kernel stack page can be mapped to
//
// The new kernel virtual memory layout:
// MAX_VA
//   ┌──────────────────────────┐
//   │ trampoline (1 page)      │  TRAMPOLINE = MAX_VA - PG_SIZE
//   ├──────────────────────────┤
//   │ kstack slot 0: guard     │
//   ├──────────────────────────┤
//   │ kstack slot 0: stack     │
//   ├──────────────────────────┤
//   │ kstack slot 1: guard     │
//   ├──────────────────────────┤
//   │ kstack slot 1: stack     │
//   ├──────────────────────────┤
//   │ ...                      │
//   └──────────────────────────┘
//   (rest of kernel mappings below)
#pragma once

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/slab.h"
#include "kernel/stl/ts_forward_list.h"
#include "kernel/util/singletony.h"

namespace xv6 {

namespace test {
void kstack_self_test();
}

// Allocates ONLY kernel virtual addresses (KVAs) for per-process kernel stacks.
// Does NOT allocate physical pages and does NOT modify page tables.
class kstack_allocator : public util::singleton<kstack_allocator> {
    friend class singleton;

  public:
    // Initialize freelist with NPROC stack KVAs under TRAMPOLINE
    void init();

    // Pop one available KVA for a stack page. Returns nullptr if empty.
    void *alloc();

    // Push a previously allocated KVA back to freelist.
    void free(void *va);

  private:
    kstack_allocator() = default;

    // Thread-safe freelist
    stl::ts_forward_list<void *> freelist_;
};

} // namespace xv6