#include "types.h"
#include "proc.h"
#include "defs.h"
#include "mlfq.h"

// Initialize all level queues to be empty at first
void mlfq_init(struct mlfq *m) {
    if (!m) {
        panic("invalid_null_pointer");
    }
    for (int i = 0; i < NLEVELS; ++i) {
        m->q[i].head = m->q[i].tail = 0;
    }
}

// Enqueue a process to a specific level queue
void mlfq_enq(struct mlfq *m, int lvl, struct proc *p) {
    if (!m || !p || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_enq");
    }

    p->rqnext = 0;
    if (m->q[lvl].tail) {
        m->q[lvl].tail->rqnext = p;
        m->q[lvl].tail = p;
    } else {
        m->q[lvl].head = m->q[lvl].tail = p;
    }
}

// Deque a process from a specific level queue
struct proc *mlfq_deq(struct mlfq *m, int lvl) {
    if (!m || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_deq");
    }

    struct proc *p = m->q[lvl].head;
    if (!p) {
        // return nullptr when the current queue is empty
        return 0;
    }

    m->q[lvl].head = p->rqnext;
    if (m->q[lvl].head == 0) {
        m->q[lvl].tail = 0;
    }

    p->rqnext = 0;
    return p;
}

// Remove a process from a specific level queue
bool mlfq_rm(struct mlfq *m, int lvl, struct proc *p) {
    if (!m || lvl < 0 || NLEVELS <= lvl) {
        panic("invalid_arguments_mlfq_rm");
    }

    if (!p) {
        // nothing to be done
        return false;
    }

    struct rqueue *rq = &m->q[lvl];
    struct proc *cur = rq->head;
    struct proc *prev = 0;

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

            cur->rqnext = 0;

            // If queue became empty, ensure tail is also NULL
            if (rq->head == 0) {
                rq->tail = 0;
            }

            return true;
        }

        prev = cur;
        cur = cur->rqnext;
    }

    // 404 not found!
    return false;
}
