// Fixed-sized slab allocator for quick allocation
#pragma once

#include "kernel/arch/riscv/riscv.h"
#include "kernel/sync/spinlock.h"
#include "kernel/util/singletony.h"

#include <array>

namespace xv6 {

namespace test {
// Let kernel see self test at boot time
void slab_self_test();
} // namespace test

// initialize all instances of slab allocator
void slabs_init();

// reclaim all allocated memory from all instances of slab allocators
void slabs_reclaim();

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
    // IMPORTANT NOTE: when you call this method, make sure it contains no live
    // objects!
    void reclaim();

  private:
    struct node;

    // helper method to request a physical page and carve out objects
    // and populate the freelist.
    // return if the physical page is allocated, the caller should handle errors
    bool make_free();

    // same as `make_free()` but build a local free list for installation later
    bool make_free_local(node *&local_free_head, void *&page);

    // Floyd cycle detection
    static bool has_cycle(node *head);

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
    static_assert(size <= PGSIZE - ((sizeof(node) + (alignof(node) - 1)) &
                                    ~(alignof(node) - 1)),
                  "slab size too large to fit any object in a page");

    // mutex to protect both lists
    spinlock lock_{};
    node *pagelist_ = nullptr;
    node *freelist_ = nullptr;

    // counter to periodically run cycle detection algorithm
    static constexpr uint64 PERIOD = 0x3FF;
    uint64 ops_ = 0;
};

template <uint64 N> consteval uint64 slab_size_for() {
    static_assert(N > 0);
    constexpr std::array<uint64, 5> classes = {32, 64, 128, 256, 512};
    for (uint64 s : classes) {
        if (N <= s) {
            return s;
        }
    }
    return 0;
}

template <uint64 N> struct slab_class_size {
    static constexpr uint64 value = slab_size_for<N>();
    static_assert(N > 0, "slab_class_size: size must be > 0");
    static_assert(value != 0, "slab_class_size: requested size > 512");
};

template <class T>
using slab_for_t = slab_allocator<slab_class_size<sizeof(T)>::value>;

template <class T> static T *slab_alloc_t() {
    return static_cast<T *>(slab_for_t<T>::instance().alloc());
}

template <class T> static void slab_free_t(T *p) {
    slab_for_t<T>::instance().free(static_cast<void *>(p));
}

} // namespace xv6
