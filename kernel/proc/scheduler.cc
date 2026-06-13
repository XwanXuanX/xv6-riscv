#include "kernel/lib/types.h"
#include "kernel/arch/riscv/riscv.h"
#include "kernel/sync/spinlock.h"
#include "kernel/proc/proc.h"
#include "kernel/lib/defs.h"
#include "kernel/proc/mlfq.h"
#include "kernel/util/assert.h"
#include "kernel/util/lock_guard.h"

namespace xv6 {

__attribute__((unused)) __attribute__((noreturn)) static void
multi_level_feedback_q() {
    cpu *c = mycpu();
    auto &feedback_q = multi_lvl_feedback_q::instance();

    c->proc = nullptr;
    for (;;) {
        intr_on();
        intr_off();

        // lock the entire MLFQ structure to prevent possible races
        feedback_q.get_lock().lock();

        // find a non-empty ready queue
        int first_non_null = feedback_q.first_non_empty();
        // there is nothing ready, use wfi to wait for interrupt
        if (first_non_null == -1) {
            feedback_q.get_lock().unlock();
            asm volatile("wfi");
            continue;
        }

        // the queue must contain at least one thing (this is the possible race
        // that we are preventing)
        assert(!feedback_q.empty(first_non_null),
               "empty ready queue (possible races)");
        // the queue is the "ready queue", meaning every process in it is in
        // READY/RUNNABLE state
        proc *const p = feedback_q.deq_locked(first_non_null);
        // snapshot current MLFQ version
        const int cur_epoch = feedback_q.get_epoch();
        // the process is dequeued, and queue is modified, no longer needs
        // protection
        feedback_q.get_lock().unlock();

        // before any read or any modification to process, protect with lock
        p->lock.lock();

        // make sure that the process is runnable and WAS in queue
        assert(p->state == RUNNABLE, "non-runnable process in ready queue");
        assert(p->in_ready_q, "runnable process not in queue");

        // the process is no longer in ready queue
        p->in_ready_q--;

        // Epoch-boost fixup (Rule 5): if stale, bounce it to the top queue.
        // IMPORTANT: do NOT run it from a low queue after a boost.
        if (p->epoch != cur_epoch) {
            p->epoch = cur_epoch;
            p->qlevel = p->qticks = 0;
            // Re-enqueue at top priority.
            {
                util::lock_guard lk(feedback_q.get_lock());
                feedback_q.enq_locked(0, p);
                assert(!p->in_ready_q, "double enq");
                p->in_ready_q++;
            }
            p->lock.unlock();
            // We've fixed up the priority
            // next time we are guaranteed to pick this or some other processes
            // from L0 Q
            continue;
        }

        // now we've asserted that p is a valid candidate, and we are about to
        // run it
        p->state = RUNNING;
        c->proc = p;

        // before actually running it, give it a new time slice
        // and reset the yield flag
        reset_time_slice(p);

        // Flush any stale TLB entry for this process's kernel stack VA before
        // context-switching in. The kstack is dynamically mapped in the shared
        // kernel page table. Another CPU may have remapped this KVA (freed the
        // old physical page and mapped a new one) since this CPU last ran a
        // process using the same kstack KVA. Without this flush, this CPU
        // would use the stale TLB entry — reading/writing the old freed page
        // — causing stack corruption and a kernel instruction page fault.
        asm volatile("sfence.vma %0, zero" : : "r"(p->kstack) : "memory");

        // context switch!
        swtch(&c->ctx, &p->ctx);

        // process p is done running
        c->proc = nullptr;

        // release the locks for both process and mlfq (in the reverse order)
        p->lock.unlock();
    }
}

// scheduler wrapper
__attribute__((noreturn)) void scheduler() { multi_level_feedback_q(); }

} // namespace xv6
