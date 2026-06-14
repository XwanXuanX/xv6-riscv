#include "kernel/lib/types.h"
#include "kernel/arch/riscv/memlayout.h"
#include "kernel/arch/riscv/riscv.h"
#include "kernel/lib/defs.h"
#include "kernel/proc/proc.h"
#include "kernel/mm/page_allocator.h"
#include "kernel/util/assert.h"
#include "kernel/mm/pagetable.h"

namespace xv6 {

/*
 * the kernel's page table.
 */
pagetable kernel_pagetable;

// extern char etext[]; // kernel.ld sets this to end of kernel code.

// extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable kvmmake() {
    const auto kpgtbl = pagetable{page_allocator::instance().alloc()};
    memset(static_cast<uint64 *>(kpgtbl), 0, PGSIZE);

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
void kvmmap(pagetable kpgtbl, const uint64 va, const uint64 pa, const uint64 sz,
            const int perm) {
    if (kpgtbl.map_pages(va, sz, pa, perm) != 0) {
        panic("kvmmap");
    }
}

// Initialize the kernel_pagetable, shared by all CPUs.
pagetable kvminit() { return kernel_pagetable = kvmmake(); }

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart() {
    // wait for any previous writes to the page table memory to finish.
    sfence_vma();

    w_satp(MAKE_SATP(static_cast<uint64 *>(kernel_pagetable)));

    // flush stale entries from the TLB.
    sfence_vma();
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable pt, uint64 dstva, const char *src, uint64 len) {
    while (len > 0) {
        const uint64 va0 = PGROUNDDOWN(dstva);
        if (va0 >= MAXVA) {
            return -1;
        }

        uint64 pa0 = pt.walk_addr(va0);
        if (pa0 == 0) {
            if ((pa0 = vmfault(pt, va0)) == 0) {
                return -1;
            }
        }

        const pte_t *pte = pagetable::walk(pt, va0, 0);
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
int copyin(pagetable pt, char *dst, uint64 srcva, uint64 len) {
    while (len > 0) {
        const uint64 va0 = PGROUNDDOWN(srcva);
        uint64 pa0 = pt.walk_addr(va0);
        if (pa0 == 0) {
            if ((pa0 = vmfault(pt, va0)) == 0) {
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
int copyinstr(pagetable pt, char *dst, uint64 srcva, uint64 max) {
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        const uint64 va0 = PGROUNDDOWN(srcva);
        const uint64 pa0 = pt.walk_addr(va0);
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

// allocate and map user memory if process is referencing a page that is either:
// 1. lazily allocated in sys_sbrk()
// 2. a new stack page that has not been allocated yet.
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64 vmfault(pagetable pt, uint64 va) {
    proc *p = myproc();
    va = PGROUNDDOWN(va);

    // Lazily allocated heap page from sys_sbrk().
    if (va < p->heap_top) {
        if (pt.is_mapped(va)) {
            return 0;
        }
        const auto mem =
            reinterpret_cast<uint64>(page_allocator::instance().alloc());
        if (mem == 0) {
            return 0;
        }
        memset(reinterpret_cast<void *>(mem), 0, PGSIZE);
        if (p->pt.map_pages(va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
            page_allocator::instance().free(reinterpret_cast<void *>(mem));
            return 0;
        }
        return mem;
    }

    // Faults inside the already-mapped stack should not reach vmfault().
    assert0(!(p->stack_bottom <= va && va < p->stack_top));

    // Grow the stack down by one page: [stack_bottom - PGSIZE, stack_bottom).
    // Rule #1: the requested va MUST immediately below current stack_bottom
    if (p->stack_bottom == 0 || va + PGSIZE != p->stack_bottom) {
        return 0;
    }
    // Rule #2: the requested va does not intrude into the guard page or heap
    if (va < p->heap_top + PGSIZE) {
        return 0;
    }
    // Rule #3: va is near SP, and SP is in the faulting page
    const uint64 sp = p->trapf->sp;
    if (sp < va || sp >= p->stack_bottom) {
        return 0;
    }
    // Rule #4: page must not be already mapped
    if (pt.is_mapped(va)) {
        return 0;
    }

    const auto mem =
        reinterpret_cast<uint64>(page_allocator::instance().alloc());
    if (mem == 0) {
        return 0;
    }
    memset(reinterpret_cast<void *>(mem), 0, PGSIZE);
    if (p->pt.map_pages(va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
        page_allocator::instance().free(reinterpret_cast<void *>(mem));
        return 0;
    }
    p->stack_bottom = va;
    return mem;
}

// Unmap stack pages below the page containing sp and move stack_bottom up.
// Returns bytes reclaimed, 0 if nothing to reclaim, -1 if sp is invalid.
int uvmstackshrink(pagetable pt, uint64 *stack_bottom, const uint64 sp,
                   const uint64 stack_top) {
    const uint64 old_bottom = *stack_bottom;
    if (sp < old_bottom || sp >= stack_top) {
        return -1;
    }

    const uint64 new_bottom = PGROUNDDOWN(sp);
    if (new_bottom <= old_bottom) {
        return 0;
    }

    const uint64 npages = (new_bottom - old_bottom) / PGSIZE;
    pt.unmap(old_bottom, npages, 1);
    *stack_bottom = new_bottom;
    return new_bottom - old_bottom;
}

// Ensure at least one guard page sits between heap_top and the stack.
// Tries stack shrink when the heap would otherwise collide with the stack.
int try_make_heap_room(proc *p, const uint64 new_heap_top) {
    if (new_heap_top <= p->stack_bottom - PGSIZE) {
        return 0;
    }
    if (uvmstackshrink(p->pt, &p->stack_bottom, p->trapf->sp, p->stack_top) <=
        0) {
        return -1;
    }
    if (new_heap_top <= p->stack_bottom - PGSIZE) {
        return 0;
    }
    return -1;
}

} // namespace xv6