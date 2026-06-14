// Global process table and PCB allocation.
#pragma once

#include "kernel/stl/ts_list.h"
#include "kernel/sync/spinlock.h"
#include "kernel/proc/proc.h"
#include "kernel/util/singletony.h"
#include "kernel/util/lock_guard.h"
#include "kernel/mm/pagetable.h"

namespace xv6 {

class proc_list : public util::singleton<proc_list> {
    friend class singleton;

  public:
    void init();

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
    [[nodiscard]] int alloc_pid();
    [[nodiscard]] bool pinit(proc *p);
    void pfree(proc *p) const;

    static pagetable alloc_ptable(proc *p);
    static void free_ptable(pagetable pt, uint64 heap_top, uint64 stack_bottom,
                            uint64 stack_top);

    proc_list() = default;

    mutable spinlock pid_lock_{};
    int next_pid_ = 1;
    stl::ts_list<proc *> procs_;
};

} // namespace xv6
