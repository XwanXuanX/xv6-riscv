#include "kernel/lib/lib_api.h"
#include "kernel/mm/slab.h"
#include "kernel/mm/kalloc.h"
#include "kernel/util/assert.h"
#include "kernel/util/lock_guard.h"

namespace xv6 {

static void mem_fill(void *p, const int v, const uint64 n) {
    auto *c = static_cast<unsigned char *>(p);
    for (uint64 i = 0; i < n; i++) {
        c[i] = static_cast<unsigned char>(v);
    }
}

template <uint64 size> bool slab_allocator<size>::has_cycle(node *head) {
    auto *slow = head;
    auto *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
            return true;
        }
    }
    return false;
}

template <uint64 size> void slab_allocator<size>::reclaim() {
    util::lock_guard lk(lock_);

    if (pagelist_ == nullptr) {
        return;
    }

    // Keep the first page in pagelist_
    node *keep = pagelist_;
    node *cur = keep->next;
    keep->next = nullptr;
    pagelist_ = keep;
    // Free all extra pages back to the page allocator
    while (cur) {
        node *nxt = cur->next;
        page_allocator::instance().free(cur);
        cur = nxt;
    }

    // Drop the freelist entirely
    freelist_ = nullptr;
    // Rebuild freelist from the kept page only
    auto add = [&](node *n) {
        n->next = freelist_;
        freelist_ = n;
    };
    uint64 addr = reinterpret_cast<uint64>(keep) + sizeof(node);
    constexpr uint64 A = alignof(node);
    addr = (addr + (A - 1)) & ~(A - 1);
    const uint64 page_end = reinterpret_cast<uint64>(keep) + PGSIZE;
    for (; addr + size <= page_end; addr += size) {
        add(reinterpret_cast<node *>(addr));
    }
}

template <uint64 size>
bool slab_allocator<size>::make_free_local(node *&local_free_head,
                                           void *&page) {
    static_assert(size >= sizeof(node));
    constexpr uint64 A = alignof(node);
    static_assert(size <= PGSIZE - ((sizeof(node) + (A - 1)) & ~(A - 1)),
                  "slab size too large after header+alignment");

    page = page_allocator::instance().alloc();
    if (page == nullptr) {
        return false;
    }

    // Build a local freelist inside this page.
    local_free_head = nullptr;
    auto add = [&](node *n) {
        n->next = local_free_head;
        local_free_head = n;
    };

    auto addr = reinterpret_cast<uint64>(page) + sizeof(node);
    addr = (addr + (A - 1)) & ~(A - 1);
    const uint64 page_end = reinterpret_cast<uint64>(page) + PGSIZE;
    for (; addr + size <= page_end; addr += size) {
        add(reinterpret_cast<node *>(addr));
    }

    return true;
}

template <uint64 size> bool slab_allocator<size>::make_free() {
    void *p = page_allocator::instance().alloc();
    if (p == nullptr) {
        return false;
    }
    const auto pn = static_cast<node *>(p);
    // add the physical page to list
    pn->next = pagelist_;
    pagelist_ = pn;

    // populate free list
    auto add = [&](node *nod) {
        nod->next = freelist_;
        freelist_ = nod;
    };
    // skip the node embedded in the header of physical page
    auto addr = reinterpret_cast<uint64>(p) + sizeof(node);
    addr = (addr + alignof(node) - 1) & ~(alignof(node) - 1);
    const uint64 page_end = reinterpret_cast<uint64>(p) + PGSIZE;
    for (; addr + size <= page_end; addr += size) {
        add(reinterpret_cast<node *>(addr));
    }

    return true;
}

template <uint64 size> void slab_allocator<size>::init() {
    // on init, we should allocate 1 physical page by default
    // and during the lifetime of this allocator, at least 1 physical page
    // should always be allocated to avoid frequent allocate and free of
    // physical pages
    lock_.init_lock("slab_allocator");

    assert0(pagelist_ == nullptr);
    assert0(freelist_ == nullptr);

    const auto allocated = make_free();
    if (!allocated) {
        panic("slab_allocator<size>::init() no free phys page");
    }
}

template <uint64 size> void slab_allocator<size>::free(void *pa) {
    if (pa == nullptr) {
        return;
    }

    const auto a = reinterpret_cast<uint64>(pa);
    assert0((a & (alignof(node) - 1)) == 0);

    // hold the lock while doing the rest of validation
    util::lock_guard lk(lock_);

    // verify pa belongs to one of our pages
    bool ok = false;
    for (node *pg = pagelist_; pg != nullptr; pg = pg->next) {
        const auto base = reinterpret_cast<uint64>(pg);
        const uint64 page_end = base + PGSIZE;

        // disallow freeing the page header
        uint64 first_obj = base + sizeof(node);
        constexpr uint64 A = alignof(node);
        first_obj = (first_obj + (A - 1)) & ~(A - 1);

        if (a >= first_obj && a + size <= page_end &&
            (a - first_obj) % size == 0) {
            ok = true;
            break;
        }
    }
    assert0(ok);

    if ((++ops_ & PERIOD) == 0) {
        assert0(lock_.holding());
        if (has_cycle(freelist_)) {
            panic("slab: freelist cycle");
        }
        if (has_cycle(pagelist_)) {
            panic("slab: pagelist cycle");
        }
    }

    // the actual freeing
    mem_fill(pa, 0xAA, size);
    auto *n = static_cast<node *>(pa);
    n->next = freelist_;
    freelist_ = n;
}

template <uint64 size> void *slab_allocator<size>::alloc() {
    // periodically do cycle detection
    if ((++ops_ & PERIOD) == 0) {
        util::lock_guard lk(lock_);
        assert0(lock_.holding());
        if (has_cycle(freelist_)) {
            panic("slab: freelist cycle");
        }
        if (has_cycle(pagelist_)) {
            panic("slab: pagelist cycle");
        }
    }

    // fast path: try under lock
    {
        util::lock_guard lk(lock_);
        if (freelist_ != nullptr) {
            node *n = freelist_;
            freelist_ = freelist_->next;
            mem_fill(n, 0xCC, size);
            return n;
        }
    }

    // slow path: request phys page and populate
    void *page = nullptr;
    node *local_free = nullptr;
    if (!make_free_local(local_free, page)) {
        return nullptr;
    }

    // install under lock
    util::lock_guard lk(lock_);
    // install allocated page
    auto *pn = static_cast<node *>(page);
    pn->next = pagelist_;
    pagelist_ = pn;
    // install free objects
    if (local_free) {
        node *tail = local_free;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = freelist_;
        freelist_ = local_free;
    }

    // now try to allocate again
    if (freelist_ == nullptr) {
        // roll back the page allocation to avoid leaking a page
        if (pagelist_ == pn) {
            pagelist_ = pagelist_->next;
        }
        page_allocator::instance().free(page);
        return nullptr;
    }
    node *n = freelist_;
    freelist_ = freelist_->next;
    mem_fill(n, 0xCC, size);
    return n;
}

void slabs_init() {
    slab_allocator<32>::instance().init();
    slab_allocator<64>::instance().init();
    slab_allocator<128>::instance().init();
    slab_allocator<256>::instance().init();
    slab_allocator<512>::instance().init();
}

void slabs_reclaim() {
    slab_allocator<32>::instance().reclaim();
    slab_allocator<64>::instance().reclaim();
    slab_allocator<128>::instance().reclaim();
    slab_allocator<256>::instance().reclaim();
    slab_allocator<512>::instance().reclaim();
}

template class slab_allocator<32>;
template class slab_allocator<64>;
template class slab_allocator<128>;
template class slab_allocator<256>;
template class slab_allocator<512>;

} // namespace xv6
