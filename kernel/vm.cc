#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "kernel/defs.h"
#include "kernel/proc.h"
#include "kernel/kalloc.h"

namespace xv6 {

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

// extern char etext[]; // kernel.ld sets this to end of kernel code.

// extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t kvmmake() {
    const auto kpgtbl =
        static_cast<pagetable_t>(page_allocator::instance().alloc());
    memset(kpgtbl, 0, PGSIZE);

    // uart registers
    kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface
    kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // PLIC
    kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

    const auto cetext = reinterpret_cast<uint64>(etext);

    // map kernel text executable and read-only.
    kvmmap(kpgtbl, KERNBASE, KERNBASE, cetext - KERNBASE, PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    // maps all usable RAM into the kernel's virtual address space with an
    // identity mapping.
    // in other words, kernel's VA == RAM's PA
    kvmmap(kpgtbl, cetext, cetext, PHYSTOP - cetext, PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(kpgtbl, TRAMPOLINE, reinterpret_cast<uint64>(trampoline), PGSIZE,
           PTE_R | PTE_X);

    return kpgtbl;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, const uint64 va, const uint64 pa,
            const uint64 sz, const int perm) {
    if (mappages(kpgtbl, va, sz, pa, perm) != 0) {
        panic("kvmmap");
    }
}

// Initialize the kernel_pagetable, shared by all CPUs.
pagetable_t kvminit() { return kernel_pagetable = kvmmake(); }

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart() {
    // wait for any previous writes to the page table memory to finish.
    sfence_vma();

    w_satp(MAKE_SATP(kernel_pagetable));

    // flush stale entries from the TLB.
    sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, const uint64 va, const int alloc) {
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

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, const uint64 va) {
    if (va >= MAXVA) {
        return 0;
    }

    const pte_t *pte = walk(pagetable, va, 0);
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

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, const uint64 va, const uint64 size,
             uint64 pa, const int perm) {
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
        if ((pte = walk(pagetable, a, 1)) == nullptr) {
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

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
    const auto pagetable =
        static_cast<pagetable_t>(page_allocator::instance().alloc());
    if (pagetable == nullptr) {
        return nullptr;
    }
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, const uint64 va, const uint64 npages,
              const int do_free) {
    pte_t *pte;

    if (va % PGSIZE != 0) {
        panic("uvmunmap: not aligned");
    }

    for (uint64 a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) ==
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

// Allocate PTEs and physical memory to grow a process from start_va to
// end_va, which need not be page aligned. Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 start_va, const uint64 end_va,
                const int xperm) {
    if (end_va < start_va) {
        return start_va;
    }

    start_va = PGROUNDUP(start_va);
    for (uint64 a = start_va; a < end_va; a += PGSIZE) {
        auto mem = static_cast<char *>(page_allocator::instance().alloc());
        if (mem == nullptr) {
            uvmdealloc(pagetable, a, start_va);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, reinterpret_cast<uint64>(mem),
                     PTE_R | PTE_U | xperm) != 0) {
            page_allocator::instance().free(mem);
            uvmdealloc(pagetable, a, start_va);
            return 0;
        }
    }
    return end_va;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, const uint64 oldsz, const uint64 newsz) {
    if (newsz >= oldsz) {
        return oldsz;
    }

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        const uint64 npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        const pte_t pte = pagetable[i];
        if (pte & PTE_V && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // this PTE points to a lower-level page table.
            const uint64 child = PTE2_PA(pte);
            freewalk(reinterpret_cast<pagetable_t>(child));
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
            panic("freewalk: leaf");
        }
    }
    page_allocator::instance().free(pagetable);
}

// Free user memory pages, including the heap and stack sections,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, const uint64 heap_top,
             const uint64 stack_bottom, const uint64 stack_top) {
    // free the text/bss/data + heap section
    if (heap_top > 0) {
        uvmunmap(pagetable, 0, PGROUNDUP(heap_top) / PGSIZE, 1);
    }
    // free the stack section
    const uint64 stack_size = stack_top - stack_bottom;
    if (stack_size > 0) {
        uvmunmap(pagetable, stack_bottom, PGROUNDUP(stack_size) / PGSIZE, 1);
    }
    freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t nw, const uint64 heap_top,
            const uint64 stack_bottom, const uint64 stack_top) {
    pte_t *pte;
    uint64 pa, i;
    uint flags;
    char *mem;

    // helper to copy over a section of memory
    auto copy_over = [&](const uint64 start, const uint64 end) -> bool {
        for (i = start; i < end; i += PGSIZE) {
            if ((pte = walk(old, i, 0)) == nullptr) {
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
            if (mappages(nw, i, PGSIZE, reinterpret_cast<uint64>(mem),
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
    uvmunmap(nw, 0, i / PGSIZE, 1);
    return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, const uint64 va) {
    pte_t *pte = walk(pagetable, va, 0);
    if (pte == nullptr) {
        panic("uvmclear");
    }
    *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, const char *src, uint64 len) {
    while (len > 0) {
        const uint64 va0 = PGROUNDDOWN(dstva);
        if (va0 >= MAXVA) {
            return -1;
        }

        uint64 pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            if ((pa0 = vmfault(pagetable, va0)) == 0) {
                return -1;
            }
        }

        const pte_t *pte = walk(pagetable, va0, 0);
        // forbid copyout over read-only user text pages.
        if ((*pte & PTE_W) == 0) {
            return -1;
        }

        uint64 n = PGSIZE - (dstva - va0);
        if (n > len) {
            n = len;
        }
        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    while (len > 0) {
        const uint64 va0 = PGROUNDDOWN(srcva);
        uint64 pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            if ((pa0 = vmfault(pagetable, va0)) == 0) {
                return -1;
            }
        }
        uint64 n = PGSIZE - (srcva - va0);
        if (n > len) {
            n = len;
        }
        memmove(dst, (void *)(pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        const uint64 va0 = PGROUNDDOWN(srcva);
        const uint64 pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            return -1;
        }
        uint64 n = PGSIZE - (srcva - va0);
        if (n > max) {
            n = max;
        }

        const char *p = (char *)(pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            }
            *dst = *p;
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    if (got_null) {
        return 0;
    }
    return -1;
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64 vmfault(pagetable_t pagetable, uint64 va) {
    const proc *p = myproc();

    if (va >= p->heap_top) {
        return 0;
    }
    va = PGROUNDDOWN(va);
    if (ismapped(pagetable, va)) {
        return 0;
    }
    const auto mem =
        reinterpret_cast<uint64>(page_allocator::instance().alloc());
    if (mem == 0) {
        return 0;
    }
    memset(reinterpret_cast<void *>(mem), 0, PGSIZE);
    if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
        page_allocator::instance().free(reinterpret_cast<void *>(mem));
        return 0;
    }
    return mem;
}

int ismapped(pagetable_t pagetable, const uint64 va) {
    const pte_t *pte = walk(pagetable, va, 0);
    if (pte == nullptr) {
        return 0;
    }
    if (*pte & PTE_V) {
        return 1;
    }
    return 0;
}

} // namespace xv6