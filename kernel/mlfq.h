#pragma once

#include "param.h"
#include "spinlock.h"
#include <array>

namespace xv6 {

// promise that we'll have this somewhere
struct proc;

class mlfq {
  public:
    mlfq() = default;

    // Initialize all level queues to be empty at first
    void init();

    // Enqueue a process to a specific level queue
    // could be used in:
    //  1. place newly created process at the top level queue
    //  2. demote a process to a lower level queue
    //  3. periodical priority boost for all processes
    void enq(int lvl, proc *p);

    // Deque a process from a specific level queue
    // return nullptr when the current queue is empty
    proc *deq(int lvl);

    // Remove a process from a specific level queue
    // could be used in:
    //  1. demote a process
    //  3. periodical priority boost
    // There could be cases where the remove target does not exist in the queue
    //  - return a bool to indicate if the removal is successful
    bool rm(int lvl, const proc *p);

    // The locked variant
    void enq_locked(int lvl, proc *p);

    // The locked variant
    proc *deq_locked(int lvl);

    // The locked variant
    bool rm_locked(int lvl, const proc *p);

    // Export the spinlock to the caller for flexibility
    spinlock &get_lock() { return lock_; }

    // Print the status of MLFQ for debugging
    // Triggered by ^P in sh
    void dump() const;

    // Get the current MLFQ version
    [[nodiscard]] int get_epoch() const { return boost_epoch_; }

    // Get the index of the first non-empty queue
    [[nodiscard]] int first_non_empty() const;

    // Test if a queue is empty
    [[nodiscard]] bool empty(const int lvl) const {
        const rqueue *const rq = &q_[lvl];
        return !(rq->head && rq->tail);
    }

  private:
    struct rqueue {
        proc *head;
        proc *tail;
    };

    std::array<rqueue, NLEVELS> q_;
    spinlock lock_;

    // The "version number" of the MLFQ
    // used for periodic priority boosting
    int boost_epoch_;
};

} // namespace xv6
