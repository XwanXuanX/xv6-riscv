// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

namespace xv6 {

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
    run *next;
};

struct {
    spinlock lock;
    run *freelist;
} kmem;

void kinit() {
    kmem.lock.init_lock("kmem");
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
    auto p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= static_cast<char *>(pa_end); p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {

    if (((uint64)pa % PGSIZE) != 0 || static_cast<char *>(pa) < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    const auto r = static_cast<struct run *>(pa);

    kmem.lock.lock();
    r->next = kmem.freelist;
    kmem.freelist = r;
    kmem.lock.unlock();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void) {

    kmem.lock.lock();
    run *r = kmem.freelist;
    if (r)
        kmem.freelist = r->next;
    kmem.lock.unlock();

    if (r)
        memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
}

} // namespace xv6