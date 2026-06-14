#include "kernel/mm/pagetable.h"
#include "kernel/mm/page_allocator.h"
#include "kernel/lib/defs.h"

namespace xv6 {

pagetable pagetable::create() {
    const auto pt =
        static_cast<pagetable_t>(page_allocator::instance().alloc());
    if (pt == nullptr) {
        return pagetable{};
    }
    memset(pt, 0, PGSIZE);
    return pagetable{pt};
}

int pagetable::is_mapped(const uint64 va) const {
    const pte_t *pte = walk(pt_, va, 0);
    if (pte == nullptr) {
        return 0;
    }
    if (*pte & PTE_V) {
        return 1;
    }
    return 0;
}

uint64 pagetable::walk_addr(const uint64 va) const {
    if (va >= MAXVA) {
        return 0;
    }
    const pte_t *pte = walk(pt_, va, 0);
    if (pte == nullptr) {
        return 0;
    }
    if ((*pte & PTE_V) == 0) {
        return 0;
    }
    if ((*pte & PTE_U) == 0) {
        return 0;
    }
    const uint64 pa = PTE2_PA(*pte);
    return pa;
}

uint64
pagetable::malloc(uint64 start_va, const uint64 end_va, const int xperm) {
    if (end_va < start_va) {
        return start_va;
    }

    start_va = PGROUNDUP(start_va);
    for (uint64 a = start_va; a < end_va; a += PGSIZE) {
        auto mem = static_cast<char *>(page_allocator::instance().alloc());
        if (mem == nullptr) {
            dealloc(a, start_va);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (map_pages(a, PGSIZE, reinterpret_cast<uint64>(mem),
                     PTE_R | PTE_U | xperm) != 0) {
            page_allocator::instance().free(mem);
            dealloc(a, start_va);
            return 0;
        }
    }
    return end_va;
}

uint64 pagetable::dealloc(const uint64 old_sz, const uint64 new_sz) {
    if (new_sz >= old_sz) {
        return old_sz;
    }

    if (PGROUNDUP(new_sz) < PGROUNDUP(old_sz)) {
        const uint64 npages = (PGROUNDUP(old_sz) - PGROUNDUP(new_sz)) / PGSIZE;
        unmap((new_sz), npages, 1);
    }

    return new_sz;
}

void pagetable::free(const uint64 heap_top, const uint64 stack_bottom,
                     const uint64 stack_top) {
    // free the text/bss/data + heap section
    if (heap_top > 0) {
        unmap(0, PGROUNDUP(heap_top) / PGSIZE, 1);
    }
    // free the stack section
    const uint64 stack_size = stack_top - stack_bottom;
    if (stack_size > 0) {
        unmap(stack_bottom, PGROUNDUP(stack_size) / PGSIZE, 1);
    }
    free_walk(pt_);
}

void pagetable::unmap(const uint64 va, const uint64 npages,
                      const int do_free) const {
    pte_t *pte;

    if (va % PGSIZE != 0) {
        panic("uvmunmap: not aligned");
    }

    for (uint64 a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pt_, a, 0)) ==
            nullptr) { // leaf page table entry allocated?
            continue;
        }
        if ((*pte & PTE_V) == 0) { // has physical page been allocated?
            continue;
        }
        if (do_free) {
            const uint64 pa = PTE2_PA(*pte);
            page_allocator::instance().free(reinterpret_cast<void *>(pa));
        }
        *pte = 0;
    }
}

int pagetable::copy(pagetable other, const uint64 heap_top,
                    const uint64 stack_bottom, const uint64 stack_top) const {
    pte_t *pte;
    uint64 pa, i;
    uint flags;
    char *mem;

    // helper to copy over a section of memory
    auto copy_over = [&](const uint64 start, const uint64 end) -> bool {
        for (i = start; i < end; i += PGSIZE) {
            if ((pte = walk(other, i, 0)) == nullptr) {
                continue; // page table entry hasn't been allocated
            }
            if ((*pte & PTE_V) == 0) {
                continue; // physical page hasn't been allocated
            }
            pa = PTE2_PA(*pte);
            flags = PTE_FLAGS(*pte);
            if ((mem = static_cast<char *>(
                     page_allocator::instance().alloc())) == nullptr) {
                return false;
            }
            memmove(mem, reinterpret_cast<char *>(pa), PGSIZE);
            if (map_pages(i, PGSIZE, reinterpret_cast<uint64>(mem),
                         static_cast<int>(flags)) != 0) {
                page_allocator::instance().free(mem);
                return false;
            }
        }
        return true;
    };

    // copy over text/bss/data + heap section
    if (!copy_over(0, heap_top)) {
        goto err;
    }
    // copy over stack section
    if (!copy_over(stack_bottom, stack_top)) {
        goto err;
    }

    return 0;

err:
    unmap(0, i / PGSIZE, 1);
    return -1;
}

int pagetable::map_pages(const uint64 va, const uint64 size, uint64 pa,
                         const int perm) const {
    pte_t *pte;

    if (va % PGSIZE != 0) {
        panic("mappages: va not aligned");
    }

    if (size % PGSIZE != 0) {
        panic("mappages: size not aligned");
    }

    if (size == 0) {
        panic("mappages: size");
    }

    uint64 a = va;
    const uint64 last = va + size - PGSIZE;
    for (;;) {
        if ((pte = walk(pt_, a, 1)) == nullptr) {
            return -1;
        }
        if (*pte & PTE_V) {
            panic("mappages: remap");
        }
        *pte = PA2_PTE(pa) | perm | PTE_V;
        if (a == last) {
            break;
        }
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

pte_t *
pagetable::walk(pagetable_t pagetable, const uint64 va, const int alloc) {
    if (va >= MAXVA) {
        panic("walk");
    }

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = reinterpret_cast<pagetable_t>(PTE2_PA(*pte));
        } else {
            if (!alloc ||
                (pagetable = static_cast<pde_t *>(
                     page_allocator::instance().alloc())) == nullptr) {
                return nullptr;
            }
            memset(pagetable, 0, PGSIZE);
            *pte = PA2_PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

pte_t *
pagetable::walk(pagetable pagetable, const uint64 va, const int alloc) {
    return walk(pagetable.pt_, va,  alloc);
}

void pagetable::free_walk(pagetable_t pagetable) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        const pte_t pte = pagetable[i];
        if (pte & PTE_V && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // this PTE points to a lower-level page table.
            const uint64 child = PTE2_PA(pte);
            free_walk(reinterpret_cast<pagetable_t>(child));
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
            panic("freewalk: leaf");
        }
    }
    page_allocator::instance().free(pagetable);
}

} // namespace xv6