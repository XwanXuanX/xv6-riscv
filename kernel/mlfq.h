#ifndef KERNEL_MLFQ_H
#define KERNEL_MLFQ_H

#include "param.h"
#include "spinlock.h"

namespace xv6 {

// promise that we'll have this somewhere
struct proc;

struct rqueue {
    struct proc *head;
    struct proc *tail;
};

struct mlfq {
    struct rqueue q[NLEVELS];
    struct spinlock lock;
    // The "version number" of the MLFQ
    // used for periodic priority boosting
    int boost_epoch;
};

//
// Safe APIs
//

// Initialize all level queues to be empty at first
void mlfq_init(struct mlfq *m);

// Enqueue a process to a specific level queue
// could be used in:
//  1. place newly created process at the top level queue
//  2. demote a process to a lower level queue
//  3. periodical priority boost for all processes
void mlfq_enq(struct mlfq *m, int lvl, struct proc *p);

// Deque a process from a specific level queue
// return nullptr when the current queue is empty
struct proc *mlfq_deq(struct mlfq *m, int lvl);

// Remove a process from a specific level queue
// could be used in:
//  1. demote a process
//  3. periodical priority boost
// There could be cases where the remove target does not exist in the queue
//  - return a bool to indicate if the removal is successful
bool mlfq_rm(struct mlfq *m, int lvl, struct proc *p);

//
// Unsafe APIs (assume locked, used in bulk operation)
//

void mlfq_enq_locked(struct mlfq *m, int lvl, struct proc *p);
struct proc *mlfq_deq_locked(struct mlfq *m, int lvl);
bool mlfq_rm_locked(struct mlfq *m, int lvl, struct proc *p);

} // namespace xv6

#endif
