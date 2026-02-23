// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include "kernel/defs.h"
#include "kernel/kalloc.h"
#include "kernel/util/lock_guard.h"

namespace xv6 {

// extern char end[]; // first address after kernel.
//                    // defined by kernel.ld.

void page_allocator::init() {
    lock_.init_lock("page_allocator");
    // mark all physical pages between `end` and `PHYSTOP` as available
    freerange(end, reinterpret_cast<void *>(PHYSTOP));
}

void page_allocator::freerange(void *pa_start, void *pa_end) {
    auto p =
        reinterpret_cast<char *>(PGROUNDUP(reinterpret_cast<uint64>(pa_start)));
    for (; p + PGSIZE <= static_cast<char *>(pa_end); p += PGSIZE) {
        free(p);
    }
}

void page_allocator::free(void *pa) {
    // since we always allocate on page size
    // the starting pointer to a page should always be page-size aligned
    const bool alignment = reinterpret_cast<uint64>(pa) % PGSIZE == 0;
    // start of physical page cannot invade kernel reserved memory space
    const bool in_kernel = static_cast<char *>(pa) < end;
    // start of physical page exceed maximum physical address available
    const bool exceed_phys = reinterpret_cast<uint64>(pa) >= PHYSTOP;

    if (!alignment || in_kernel || exceed_phys) {
        panic("page_allocator::free");
    }

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    // Add the physical page to freelist
    {
        // note that you use the first few bytes in the page as list node
        // to embed a free list within the free space
        const auto r = static_cast<node *>(pa);
        util::lock_guard lk(lock_);
        r->next = freelist_;
        freelist_ = r;
    }
}

void *page_allocator::alloc() {
    node *const r = [this] {
        util::lock_guard lk(lock_);
        // pop from list head
        node *const ret = freelist_;
        if (ret) {
            freelist_ = ret->next;
        }
        return ret;
    }();

    if (r) {
        memset(r, 5, PGSIZE); // fill with junk
    }
    return r;
}

} // namespace xv6
