#include "kernel/defs.h"
#include "kernel/proc_list.h"
#include "kernel/slab.h"
#include "kernel/util/lock_guard.h"
#include "kernel/util/assert.h"

namespace xv6 {

void process_list::init() {
    pid_lock_.init_lock("proc_list::pid_lock");
    next_pid_ = 1;
}

proc *process_list::alloc_proc() { return nullptr; }

void process_list::retire_proc(proc *p) {}

void process_list::destroy_proc(proc *p) {}

proc *process_list::find_pid(const int pid) const {
    auto &list = const_cast<stl::ts_list<proc *> &>(procs_);
    for (auto view = list.locked(); proc *p : view) {
        // although PID should be immutable after process creation
        // we still lock the per-process mutex to guarantee system integrity
        util::lock_guard lk(p->lock);
        if (p->pid == pid) {
            return p;
        }
    }
    return nullptr;
}

uint64 process_list::count() const { return procs_.size(); }

int process_list::alloc_pid() {
    util::lock_guard lk(pid_lock_);
    return next_pid_++;
}

} // namespace xv6