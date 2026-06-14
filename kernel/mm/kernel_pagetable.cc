#include "kernel/mm/kernel_pagetable.h"
#include "kernel/mm/page_allocator.h"
#include "kernel/lib/defs.h"
#include "kernel/arch/riscv/memlayout.h"

namespace xv6 {

void kernel_pagetable::init() {
    kpt_ = make();
}

pagetable kernel_pagetable::make() {
    const auto kpt = pagetable{page_allocator::instance().alloc()};
    memset(static_cast<uint64 *>(kpt), 0, PGSIZE);

    // uart registers
    kvmmap(kpt, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface
    kvmmap(kpt, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // PLIC
    kvmmap(kpt, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

    const auto cetext = reinterpret_cast<uint64>(etext);

    // map kernel text executable and read-only.
    kvmmap(kpt, KERNBASE, KERNBASE, cetext - KERNBASE, PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    // maps all usable RAM into the kernel's virtual address space with an
    // identity mapping.
    // in other words, kernel's VA == RAM's PA
    kvmmap(kpt, cetext, cetext, PHYSTOP - cetext, PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(kpt, TRAMPOLINE, reinterpret_cast<uint64>(trampoline), PGSIZE,
           PTE_R | PTE_X);

    return kpt;
}

void kernel_pagetable::map(const uint64 va, const uint64 pa, const uint64 sz, const int perm) const {
    if (kpt_.map(va, sz, pa, perm) != 0) {
        panic("kvmmap");
    }
}

} // namespace xv6
