#ifndef KERNEL_MLFQ_H
#define KERNEL_MLFQ_H

#include "param.h"
#include "spinlock.h"
#include <array>

namespace xv6 {

// promise that we'll have this somewhere
struct proc;

struct rqueue {
    proc *head;
    proc *tail;
};

struct mlfq {
    std::array<rqueue, NLEVELS> q;
    spinlock lock;
    // The "version number" of the MLFQ
    // used for periodic priority boosting
    int boost_epoch;
};

//
// Safe APIs
//

// Initialize all level queues to be empty at first
void mlfq_init(mlfq *m);

// Enqueue a process to a specific level queue
// could be used in:
//  1. place newly created process at the top level queue
//  2. demote a process to a lower level queue
//  3. periodical priority boost for all processes
void mlfq_enq(mlfq *m, int lvl, proc *p);

// Deque a process from a specific level queue
// return nullptr when the current queue is empty
proc *mlfq_deq(mlfq *m, int lvl);

// Remove a process from a specific level queue
// could be used in:
//  1. demote a process
//  3. periodical priority boost
// There could be cases where the remove target does not exist in the queue
//  - return a bool to indicate if the removal is successful
bool mlfq_rm(mlfq *m, int lvl, proc *p);

//
// Unsafe APIs (assume locked, used in bulk operation)
//

void mlfq_enq_locked(mlfq *m, int lvl, proc *p);
proc *mlfq_deq_locked(mlfq *m, int lvl);
bool mlfq_rm_locked(mlfq *m, int lvl, const proc *p);

} // namespace xv6

#endif
