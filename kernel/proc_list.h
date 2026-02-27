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

    // free proc object memory and remove from process list
    void free_proc(proc *p);

    // find process by pid; returns with p->lock NOT held
    [[nodiscard]] proc *find_pid(int pid) const;

    // iterate through all processes, calling fn(p) with p->lock held
    template <class Fn> void for_each_locked(Fn &&fn) {
        auto view = const_cast<stl::ts_list<proc *> &>(procs_).locked();
        for (proc *p : view) {
            util::lock_guard proc_lk(p->lock);
            fn(p);
        }
    }

    [[nodiscard]] uint64 count() const;

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
    static void free_ptable(pagetable_t page, uint64 sz);

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