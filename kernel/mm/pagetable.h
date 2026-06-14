#pragma once

#include "kernel/lib/types.h"
#include "kernel/arch/riscv/riscv.h"

namespace xv6 {

class pagetable {
  public:
    using pagetable_t = uint64 *; // 512 PTEs

    pagetable() = default;
    explicit pagetable(pagetable_t pt) : pt_(pt) {}
    explicit pagetable(void* pt) : pt_(static_cast<pagetable_t>(pt)) {}

    explicit operator uint64*() const { return this->pt_; }

    // create an empty user page table.
    // returns 0 if out of memory.
    static pagetable create();

    // check if the virtual address belongs to a mapped page in this pagetable
    [[nodiscard]] int is_mapped(uint64 va) const;

    // Look up a virtual address, return the physical address,
    // or 0 if not mapped.
    // Can only be used to look up user pages.
    [[nodiscard]] uint64 walk_addr(uint64 va) const;

    // Allocate PTEs and physical memory to grow a process from start_va to
    // end_va, which need not be page aligned. Returns new size or 0 on error.
    uint64 malloc(uint64 start_va, uint64 end_va, int xperm);

    // Deallocate user pages to bring the process size from old_sz to
    // new_sz. old_sz and new_sz need not be page-aligned, nor does new_sz
    // need to be less than old_sz.  old_sz can be larger than the actual
    // process size.  Returns the new process size.
    uint64 dealloc(uint64 old_sz, uint64 new_sz);

    // Free user memory pages, including the heap and stack sections,
    // then free page-table pages.
    void free(uint64 heap_top, uint64 stack_bottom, uint64 stack_top);

    // Remove npages of mappings starting from va. va must be
    // page-aligned. It's OK if the mappings don't exist.
    // Optionally free the physical memory.
    void unmap(uint64 va, uint64 npages, int do_free) const;

    // Given a parent process's page table, copy
    // its memory into a child's page table.
    // Copies both the page table and the
    // physical memory.
    // returns 0 on success, -1 on failure.
    // frees any allocated pages on failure.
    int copy(pagetable other, uint64 heap_top, uint64 stack_bottom,
             uint64 stack_top) const;

    // Create PTEs for virtual addresses starting at va that refer to
    // physical addresses starting at pa.
    // va and size MUST be page-aligned.
    // Returns 0 on success, -1 if walk() couldn't
    // allocate a needed page-table page.
    [[nodiscard]] int
    map_pages(uint64 va, uint64 size, uint64 pa, int perm) const;

    // Check if the page table is null
    [[nodiscard]] bool is_null() const { return pt_ == nullptr; }

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
    static pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
    static pte_t *walk(pagetable pagetable, uint64 va, int alloc);

  private:
    // Recursively free page-table pages.
    // All leaf mappings must already have been removed.
    static void free_walk(pagetable_t pagetable);

  private:
    pagetable_t pt_{nullptr};
};

} // namespace xv6
