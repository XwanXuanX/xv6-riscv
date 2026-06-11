#pragma once

#include "kernel/stl/ts_list.h"
#include "kernel/spinlock.h"
#include "kernel/proc.h"
#include "kernel/util/singletony.h"
#include "kernel/util/lock_guard.h"

namespace xv6 {

class process_list : public util::singleton<process_list> {
    friend class singleton;

  public:
    void init(pagetable_t kernel_ptable);

    // allocate a new proc object and insert it into the global process list
    // returns nullptr on failure
    proc *alloc_proc();

    // detach the process from global list and free the resource it's holding
    // DOES NOT free the PCB, the caller needs to handle that.
    // requires procs_ lock held AND p->lock held
    void detach_and_pfree(proc *p);

    // free proc object memory and remove from process list
    void free_proc(proc *p);

    // return the number of processes in list
    [[nodiscard]] uint64 count() const;

    template <class Fn> auto with_list_locked(Fn &&fn) const {
        auto view = const_cast<stl::ts_list<proc *> &>(procs_).locked();
        return fn(view); // caller can iterate and can early-return
    }

  private:
    // allocate a unique pid for newly created process
    [[nodiscard]] int alloc_pid();

    // initialize required fields for newly created process
    [[nodiscard]] bool pinit(proc *p);

    // free the allocated fields for a process
    void pfree(proc *p) const;

    // allocate a page table for a process
    static pagetable_t alloc_ptable(proc *p);

    // free a process's page table
    static void free_ptable(pagetable_t page, uint64 heap_top,
                            uint64 stack_bottom, uint64 stack_top);

    process_list() = default;

    // protects next_pid_
    mutable spinlock pid_lock_{};
    int next_pid_ = 1;

    // kernel's page table, used to map kernel stacks
    pagetable_t kernel_ptable_ = nullptr;

    // global process list
    stl::ts_list<proc *> procs_;
};

} // namespace xv6