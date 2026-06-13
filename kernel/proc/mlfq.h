// Multi-level feedback queue scheduler state.
#pragma once

#include "kernel/lib/param.h"
#include "kernel/sync/spinlock.h"
#include "kernel/util/assert.h"
#include "kernel/util/singletony.h"

#include <array>

namespace xv6 {

struct proc;

class mlfq : public util::singleton<mlfq> {
    friend class singleton;

  public:
    void init();

    void enq(int lvl, proc *p);
    proc *deq(int lvl);
    bool rm(int lvl, const proc *p);

    void enq_locked(int lvl, proc *p);
    proc *deq_locked(int lvl);
    bool rm_locked(int lvl, const proc *p);

    spinlock &get_lock() { return lock_; }

    void dump() const;

    [[nodiscard]] int get_epoch() const { return boost_epoch_; }

    void inc_epoch() {
        assert(lock_.holding(), "lock not held when inc epoch");
        boost_epoch_++;
    }

    [[nodiscard]] int first_non_empty() const;

    [[nodiscard]] bool empty(const int lvl) const {
        const queue *const rq = &q_[lvl];
        return !(rq->head && rq->tail);
    }

  private:
    mlfq() = default;

    struct queue {
        proc *head;
        proc *tail;
    };

    std::array<queue, NLEVELS> q_;
    spinlock lock_;
    int boost_epoch_;
};

extern std::array<int, NLEVELS> quantum;

void reset_time_slice(proc *p);
void enqueue_runnable(int lvl, proc *p);
void make_runnable_at_top(proc *p);
void boost_runnable_to_top(proc *p, int epoch);

} // namespace xv6
