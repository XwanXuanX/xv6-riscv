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
    void init();

    // allocate a new proc object and insert it into the global process list
    // returns nullptr on failure
    proc *alloc_proc();

    // remove p from process list, but do not free the proc object itself
    void retire_proc(proc *p);

    // actually free proc object memory
    void destroy_proc(proc *p);

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

    [[nodiscard]] int count() const;

  private:
    [[nodiscard]] int alloc_pid();

    process_list() = default;

    // protects next_pid_
    mutable spinlock pid_lock_{};
    int next_pid_ = 1;

    // number of active processes
    mutable spinlock count_lock_{};
    int nproc_ = 0;

    // global process list
    stl::ts_list<proc *> procs_;
};

} // namespace xv6