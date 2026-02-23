// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
#pragma once
#include "kernel/spinlock.h"
#include "kernel/util/singletony.h"

namespace xv6 {

class page_allocator : public util::singleton<page_allocator> {
    friend class singleton;

  public:
    // initialize page allocator on boot
    void init();

    // Free the page of physical memory pointed at by pa,
    // which normally should have been returned by a
    // call to page_allocator::alloc(). (The exception is when
    // initializing the allocator; page_allocator::init() and its
    // helpers may call free() directly to populate the free list.)
    void free(void *pa);

    // Allocate one 4096-byte page of physical memory.
    // Returns a pointer that the kernel can use.
    // Returns 0 if the memory cannot be allocated.
    void *alloc();

  private:
    page_allocator() = default;

    // helper to add all empty phys pages to free list on boot
    void freerange(void *pa_start, void *pa_end);

    struct node {
        node *next;
    };

    // mutex to protect the list of free phys pages
    spinlock lock_{};
    // the list of free phys pages
    node *freelist_ = nullptr;
};

} // namespace xv6