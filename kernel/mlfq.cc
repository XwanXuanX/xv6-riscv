#include "spinlock.h"
#include "defs.h"
#include "proc.h"
#include "mlfq.h"
#include "kernel/util/assert.h"
#include "kernel/util/lock_guard.h"

namespace xv6 {

void multi_lvl_feedback_q::init() {
    // For initialization, we don't lock
    lock_.init_lock("MLFQ_lock");
    for (int i = 0; i < NLEVELS; ++i) {
        q_[i].head = q_[i].tail = nullptr;
    }
    // start from version 0
    boost_epoch_ = 0;
}

void multi_lvl_feedback_q::enq_locked(const int lvl, proc *p) {
    if (!p || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_enq");
    }
    assert(lock_.holding(), "mlfq_enq_locked m_lock");
    assert(p->lock.holding(), "mlfq_enq_locked p->lock");

    p->rqnext = nullptr;
    if (q_[lvl].tail) {
        q_[lvl].tail->rqnext = p;
        q_[lvl].tail = p;
    } else {
        q_[lvl].head = q_[lvl].tail = p;
    }
}

void multi_lvl_feedback_q::enq(const int lvl, proc *p) {
    if (!p || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_enq");
    }
    {
        util::lock_guard lk(lock_);
        enq_locked(lvl, p);
    }
}

proc *multi_lvl_feedback_q::deq_locked(const int lvl) {
    if (lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_deq");
    }
    assert(lock_.holding(), "mlfq_deq_locked m->lock");

    proc *p = q_[lvl].head;
    if (!p) {
        // return nullptr when the current queue is empty
        return nullptr;
    }

    // modify the queue with lock hold is fine
    q_[lvl].head = p->rqnext;
    if (q_[lvl].head == nullptr) {
        q_[lvl].tail = nullptr;
    }

    // IMPORTANT:
    //  Notice here that we modified the process member variable
    //  WITHOUT holding the process lock, is this even valid???
    // answer is: Yes. Because `p->rqnext` is NOT a process state,
    // it's actually the MLFQ's state, which is protected by the m->lock!
    // Thus, since we are holding m->lock, this is valid.
    p->rqnext = nullptr;
    return p;
}

// Deque a process from a specific level queue
proc *multi_lvl_feedback_q::deq(const int lvl) {
    if (lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_deq");
    }

    proc *const p = [&] {
        util::lock_guard lk(lock_);
        return deq_locked(lvl);
    }();

    return p;
}

// Remove a process from a specific level queue
bool multi_lvl_feedback_q::rm_locked(const int lvl, const proc *p) {
    if (lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_rm");
    }
    assert(lock_.holding(), "mlfq_rm_locked m->lock");

    if (!p) {
        // nothing to be done
        return false;
    }

    assert(p->lock.holding(), "mlfq_rm_locked p->lock");
    queue *rq = &q_[lvl];
    proc *cur = rq->head;
    proc *prev = nullptr;

    while (cur) {
        if (cur == p) {
            // Unlink from the singly-linked list
            if (prev) {
                prev->rqnext = cur->rqnext;
            } else {
                rq->head = cur->rqnext;
            }

            // Fix tail if we removed the last element
            if (rq->tail == cur) {
                rq->tail = prev;
            }

            cur->rqnext = nullptr;

            // If queue became empty, ensure tail is also NULL
            if (rq->head == nullptr) {
                rq->tail = nullptr;
            }

            return true;
        }

        prev = cur;
        cur = cur->rqnext;
    }

    // 404 not found!
    return false;
}

bool multi_lvl_feedback_q::rm(const int lvl, const proc *p) {
    if (lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_rm");
    }
    if (!p) {
        return false;
    }

    const bool ok = [&] {
        util::lock_guard lk(lock_);
        return rm_locked(lvl, p);
    }();

    return ok;
}

void multi_lvl_feedback_q::dump() const {
    printf("MLFQ:\n");
    for (int lvl = 0; lvl < NLEVELS; lvl++) {
        printf("  L%d:", lvl);
        int cnt = 0;
        const proc *p = q_[lvl].head;

        // Print at most 30 entries per level to avoid flooding.
        while (p && cnt < 30) {
            printf(" %d", p->pid);
            p = p->rqnext;
            cnt++;
        }
        if (p) {
            printf(" ...");
        }
        printf("\n");
    }
}

int multi_lvl_feedback_q::first_non_empty() const {
    // IMPORTANT NOTE: the method does not lock itself
    // it assumes that the caller will lock before call it
    assert(lock_.holding(), "lock not held when searching");

    for (int i = 0; i < NLEVELS; ++i) {
        const queue *rq = &q_[i];
        // We should do some validation to ensure queue integrity
        if ((rq->head && !rq->tail) || (!rq->head && rq->tail)) {
            panic("inconsistent head and tail");
        }
        if (rq->head && rq->tail) {
            return i;
        }
    }
    return -1;
}

} // namespace xv6
