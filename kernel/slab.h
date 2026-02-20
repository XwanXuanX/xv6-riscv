// Fixed-sized slab allocator for quick allocation
#pragma once

#include "riscv.h"
#include "spinlock.h"
#include "utility/singletony.h"

namespace xv6 {

namespace test {
// Let kernel see self test at boot time
void slab_self_test();
} // namespace test

template <uint64 size>
class slab_allocator : public util::singleton<slab_allocator<size>> {
    friend class singleton;

  public:
    // initialize slab allocator on boot
    void init();

    // free the object pointed by pa, and added to the freelist
    void free(void *pa);

    // allocate one object and return its pointer
    void *alloc();

    // throw away the freelist, free all pages except one, and rebuild freelist
    // IMPORTANT NOTE: when you call this method, make sure it contains no live objects!
    void reclaim();

  private:
    struct node;

    // helper method to request a physical page and carve out objects
    // and populate the freelist.
    // return if the physical page is allocated, the caller should handle errors
    bool make_free();

    // same as `make_free()` but build a local free list for installation latter
    bool make_free_local(node *&local_free_head, void *&page);

    // two types of linked-list:
    // 1. list of allocated physical pages
    // 2. free list of carved, fixed-sized objects
    // both share the same list node structure
    struct node {
        node *next;
    };
    // embed a node inside a page, make sure it's large enough
    static_assert(sizeof(node) <= PGSIZE, "PGSIZE cannot contain a node");
    // embed a node inside a freed object, make sure it's large enough
    static_assert(sizeof(node) <= size, "free obj cannot contain a node");
    static_assert((size & (alignof(node) - 1)) == 0,
                  "slab size must be node-aligned");
    static_assert(size <= PGSIZE - sizeof(node),
                  "slab size too large to fit any object in a page");

    // mutex to protect both lists
    spinlock lock_{};
    node *pagelist_ = nullptr;
    node *freelist_ = nullptr;
};

} // namespace xv6
