#include "spinlock.h"
#include "defs.h"
#include "proc.h"
#include "mlfq.h"

namespace xv6 {

// little helper
static void assert(const bool cond, const char *msg) {
    if (!cond) {
        panic(msg);
    }
}

// Initialize all level queues to be empty at first
void mlfq_init(struct mlfq *m) {
    if (!m) {
        panic("invalid_null_pointer");
    }

    // For initialization, we don't lock
    m->lock.init_lock("MLFQ_lock");
    for (int i = 0; i < NLEVELS; ++i) {
        m->q[i].head = m->q[i].tail = nullptr;
    }
    // start from version 0
    m->boost_epoch = 0;
}

void mlfq_enq_locked(struct mlfq *m, const int lvl, struct proc *p) {
    if (!m || !p || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_enq");
    }
    assert(m->lock.holding(), "mlfq_enq_locked m_lock");
    assert(p->lock.holding(), "mlfq_enq_locked p->lock");

    p->rqnext = nullptr;
    if (m->q[lvl].tail) {
        m->q[lvl].tail->rqnext = p;
        m->q[lvl].tail = p;
    } else {
        m->q[lvl].head = m->q[lvl].tail = p;
    }
}

// Enqueue a process to a specific level queue
void mlfq_enq(struct mlfq *m, const int lvl, struct proc *p) {
    if (!m || !p || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_enq");
    }
    m->lock.lock();
    mlfq_enq_locked(m, lvl, p);
    // we are done with queue, release the lock
    m->lock.unlock();
}

struct proc *mlfq_deq_locked(struct mlfq *m, const int lvl) {
    if (!m || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_deq");
    }
    assert(m->lock.holding(), "mlfq_deq_locked m->lock");

    struct proc *p = m->q[lvl].head;
    if (!p) {
        // return nullptr when the current queue is empty
        return nullptr;
    }

    // modify the queue with lock hold is fine
    m->q[lvl].head = p->rqnext;
    if (m->q[lvl].head == nullptr) {
        m->q[lvl].tail = nullptr;
    }

    // IMPORTANT:
    //  Notice here that we modified the process member variable
    //  WITHOUT holding the process lock, is this even valid???
    // answer is: Yes. Because `p->rqnext` is NOT a process state,
    // it's actually the MLFQ's state, which is protected by the m->lock!
    // Thus since we are holding m->lock, this is valid.
    p->rqnext = nullptr;
    return p;
}

// Deque a process from a specific level queue
struct proc *mlfq_deq(struct mlfq *m, const int lvl) {
    if (!m || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_deq");
    }

    m->lock.lock();
    struct proc *p = mlfq_deq_locked(m, lvl);
    m->lock.unlock();

    return p;
}

// Remove a process from a specific level queue
bool mlfq_rm_locked(struct mlfq *m, const int lvl, struct proc *p) {
    if (!m || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_rm");
    }
    assert(m->lock.holding(), "mlfq_rm_locked m->lock");

    if (!p) {
        // nothing to be done
        return false;
    }

    assert(p->lock.holding(), "mlfq_rm_locked p->lock");
    struct rqueue *rq = &m->q[lvl];
    struct proc *cur = rq->head;
    struct proc *prev = nullptr;

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

bool mlfq_rm(struct mlfq *m, const int lvl, struct proc *p) {
    if (!m || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_rm");
    }
    if (!p) {
        return false;
    }

    m->lock.lock();
    bool ok = mlfq_rm_locked(m, lvl, p);
    m->lock.unlock();

    return ok;
}

} // namespace xv6
