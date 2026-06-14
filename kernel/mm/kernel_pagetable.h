#pragma once

#include "kernel/util/singletony.h"
#include "kernel/mm/pagetable.h"

namespace xv6 {

class kernel_pagetable : public util::singleton<kernel_pagetable> {
    friend class singleton;

public:
    // Initialize the kernel_pagetable, shared by all CPUs.
    void init();

    // add a mapping to the kernel page table.
    // only used when booting.
    // does not flush TLB or enable paging.
    void map( uint64 va,  uint64 pa,  uint64 sz, int perm) const;

private:
    // Make a direct-map page table for the kernel.
    static pagetable make();

private:
    pagetable kpt_{};
};

}  // namespace xv6
