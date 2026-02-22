#include "kernel/kstack_alloc.h"
#include "kernel/memlayout.h"
#include "kernel/defs.h"
#include "utility/assert.h"

namespace xv6 {

void kstack_allocator::init() {
    freelist_.clear();

    for (uint64 i = 0; i < NPROC; i++) {
        auto stack_kva = reinterpret_cast<void *>(KSTACK(i));
        freelist_.push_front(stack_kva);
    }
}

void *kstack_allocator::alloc() {
    void *kva = nullptr;
    // atomically pop one entry from list
    if (!freelist_.pop_front_value(kva)) {
        return nullptr;
    }
    return kva;
}

void kstack_allocator::free(void *va) {
    assert(va, "kstack_allocator::free: nullptr");
    // must be page-aligned
    assert((reinterpret_cast<uint64>(va) % PGSIZE) == 0,
           "kstack_allocator::free: unaligned kva");
    // must be in the kstack region below TRAMPOLINE
    assert(reinterpret_cast<uint64>(va) < TRAMPOLINE,
           "kstack_allocator::free: kva >= TRAMPOLINE");

    // atomically push one entry to list
    freelist_.push_front(va);
}

} // namespace xv6