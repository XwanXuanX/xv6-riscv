#include "kernel/mm/kstack_allocator.h"
#include "kernel/arch/riscv/memlayout.h"
#include "kernel/lib/lib_api.h"
#include "kernel/mm/slab_allocator.h"
#include "kernel/util/assert.h"
#include "kernel/util/lock_guard.h"

namespace xv6 {

bool kstack_kva_valid(const uint64 va) {
    if (va == 0 || (va % PGSIZE) != 0) {
        return false;
    }
    if (va >= TRAMPOLINE) {
        return false;
    }
    const uint64 delta = TRAMPOLINE - va;
    if (delta < KSTACK_SLOT_SIZE) {
        return false;
    }
    if (delta % KSTACK_SLOT_SIZE != 0) {
        return false;
    }
    if (va < KSTACK_VA_FLOOR) {
        return false;
    }
    return true;
}

void kstack_allocator::init() {
    if (!lock_inited_) {
        lock_.init_lock("kstack_allocator");
        lock_inited_ = true;
    }
    util::lock_guard lk(lock_);
    while (freelist_ != nullptr) {
        free_entry *e = freelist_;
        freelist_ = e->next;
        slab_free_t<free_entry>(e);
    }
    next_slot_ = 0;
}

void *kstack_allocator::alloc_locked() {
    if (freelist_ != nullptr) {
        free_entry *e = freelist_;
        freelist_ = e->next;
        const uint64 kva = e->stack_kva;
        slab_free_t<free_entry>(e);
        assert(kstack_kva_valid(kva), "kstack_allocator: bad freelist kva");
        return reinterpret_cast<void *>(kva);
    }

    const uint64 kva = KSTACK_SLOT_VA(next_slot_);
    if (!kstack_kva_valid(kva)) {
        return nullptr;
    }
    next_slot_++;
    return reinterpret_cast<void *>(kva);
}

void *kstack_allocator::alloc() {
    util::lock_guard lk(lock_);
    return alloc_locked();
}

void kstack_allocator::free_locked(void *va) {
    assert(va != nullptr, "kstack_allocator::free: nullptr");
    const auto kva = reinterpret_cast<uint64>(va);
    assert(kstack_kva_valid(kva), "kstack_allocator::free: invalid kva");

    auto *e = slab_alloc_t<free_entry>();
    assert(e != nullptr, "kstack_allocator::free: OOM for freelist node");
    e->stack_kva = kva;
    e->next = freelist_;
    freelist_ = e;
}

void kstack_allocator::free(void *va) {
    util::lock_guard lk(lock_);
    free_locked(va);
}

} // namespace xv6