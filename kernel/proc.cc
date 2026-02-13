#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.hh"
#include "proc.h"
#include "defs.h"
#include "mlfq.h"

namespace xv6 {

// little helper
static void assert(const bool cond, char *msg) {
    if (!cond) {
        panic(msg);
    }
}

struct cpu cpus[NCPU];

struct proc proc[NPROC];

// The multi-level queue in MLFQ (contains lock)
// Shared across ALL CPUs (allocated in data/BSS segment)
// one single instance for the whole kernel
struct mlfq mlq;

struct proc *initproc;

int nextpid = 1;
spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern int quantum[NLEVELS]; // trap.c
extern char trampoline[];    // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        char *pa = reinterpret_cast<char*>(kalloc());
        if (pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int)(p - proc));
        kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// initialize the proc table.
void procinit(void) {
    struct proc *p;

    pid_lock.init_lock("nextpid");
    wait_lock.init_lock("wait_lock");
    for (p = proc; p < &proc[NPROC]; p++) {
        p->lock.init_lock("proc");
        p->state = UNUSED;
        p->kstack = KSTACK((int)(p - proc));
    }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid() {
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void) {
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void) {
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int allocpid() {
    int pid;

    pid_lock.lock();
    pid = nextpid;
    nextpid = nextpid + 1;
    pid_lock.unlock();

    return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void) {
    struct proc *p;

    // prob all the locations, for each location
    //  1. pretend it is available
    //  2. acquire the lock
    //  3. if ok: proceed to setup with lock held
    //  4. if no: release the lock immediately
    for (p = proc; p < &proc[NPROC]; p++) {
        p->lock.lock();
        if (p->state == UNUSED) {
            goto found;
        } else {
            p->lock.unlock();
        }
    }
    return 0;

found:
    //
    // The lock is held while setup
    //
    p->pid = allocpid();
    p->state = USED;

    // Allocate a trapframe page.
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
        freeproc(p);
        p->lock.unlock();
        return 0;
    }

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0) {
        freeproc(p);
        p->lock.unlock();
        return 0;
    }

    // Initialize MLFQ related fields
    // DO NOT enqueue JUST YET!
    p->qlevel = p->qticks = 0;
    p->rqnext = 0;
    p->in_ready_q = 0;
    p->epoch = 0;

    // Per-level quantum
    // the slice_left and need_yield field will be set when a process becomes
    // RUNNABLE, so initialize with 0 for now
    p->slice_left = 0;
    p->need_yield = 0;

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    //
    // Remember! The process lock is still being held!
    // Someone needs to release it later!
    //
    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p) {
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
    p->qlevel = p->qticks = 0;
    p->rqnext = 0;
    p->in_ready_q = 0;
    p->epoch = 0;
    p->slice_left = 0;
    p->need_yield = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p) {
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE,
                 (uint64)trampoline, PTE_R | PTE_X) < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe page just below the trampoline page, for
    // trampoline.S.
    if (mappages(pagetable, TRAPFRAME, PGSIZE,
                 (uint64)(p->trapframe), PTE_R | PTE_W) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// Set up first user process.
void userinit(void) {
    struct proc *p;

    p = allocproc();
    // Process lock is still held
    initproc = p;

    p->cwd = namei("/");

    p->state = RUNNABLE;
    p->qlevel = p->qticks = 0;
    // each scheduling round a process starts with a fresh quanta
    p->slice_left = quantum[p->qlevel];
    p->need_yield = 0;

    // Enqueue!
    mlq.lock.lock();
    {
        // New job enters, put at the top
        mlfq_enq_locked(&mlq, 0, p);
        assert(!p->in_ready_q, "double enq");
        p->in_ready_q++;
    }
    mlq.lock.unlock();

    p->lock.unlock();
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n) {
    uint64 sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0) {
        if (sz + n > TRAPFRAME) {
            return -1;
        }
        if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
            return -1;
        }
    } else if (n < 0) {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int kfork(void) {
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        np->lock.unlock();
        return -1;
    }
    np->sz = p->sz;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    np->lock.unlock();

    wait_lock.lock();
    np->parent = p;
    wait_lock.unlock();

    np->lock.lock();
    {
        // set the new process state as runnable
        np->state = RUNNABLE;
        np->qlevel = np->qticks = 0;
        // each scheduling round a process starts with a fresh quanta
        np->slice_left = quantum[np->qlevel];
        np->need_yield = 0;
        // Enqueue!
        mlq.lock.lock();
        {
            // New job enters, put at the top
            mlfq_enq_locked(&mlq, 0, np);
            assert(!np->in_ready_q, "double enq");
            np->in_ready_q++;
        }
        mlq.lock.unlock();
    }
    np->lock.unlock();

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p) {
    struct proc *pp;

    for (pp = proc; pp < &proc[NPROC]; pp++) {
        if (pp->parent == p) {
            pp->parent = initproc;
            wakeup(initproc);
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void kexit(int status) {
    // get the currently running process
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    // FS operations (we don't care about this for now...)
    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    // acq wait_lock to manipulate the parent and child relations
    wait_lock.lock();

    // Give any children to init (for reap later).
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    // acq process lock to modify process status
    p->lock.lock();

    p->xstate = status;
    p->state = ZOMBIE;

    // parent child relation stable, safe to unlock
    wait_lock.unlock();

    // Jump into the scheduler, never to return.
    // Note that process lock is still held, this is to satisfy the assumption in the scheduler
    // See comments in scheduler() for more details
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int kwait(uint64 addr) {
    struct proc *pp;
    int havekids, pid;
    struct proc *p = myproc();

    wait_lock.lock();

    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (pp = proc; pp < &proc[NPROC]; pp++) {
            if (pp->parent == p) {
                // make sure the child isn't still in exit() or swtch().
                pp->lock.lock();

                havekids = 1;
                if (pp->state == ZOMBIE) {
                    // Found one.
                    pid = pp->pid;
                    if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                             sizeof(pp->xstate)) < 0) {
                        pp->lock.unlock();
                        wait_lock.unlock();
                        return -1;
                    }
                    freeproc(pp);
                    pp->lock.unlock();
                    wait_lock.unlock();
                    return pid;
                }
                pp->lock.unlock();
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || killed(p)) {
            wait_lock.unlock();
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run (a RUNNABLE process).
//  - swtch to start running that process.
//  - eventually that process transfers control (via yield/sleep/exit) via swtch back to the scheduler.
// Ensure correctness with
//  - process locks (p->lock)
//  - interrupt enable/disable
__attribute__((unused)) __attribute__((noreturn)) static void round_robin(void) {
    // the iterator
    struct proc *p;
    // current CPU's status, including `c->proc`, the CPU's currently running process
    struct cpu *c = mycpu();

    // in initialization, the CPU is not running anything
    c->proc = 0;
    for (;;) {
        // The most recent process to run may have had interrupts
        // turned off; enable them to avoid a deadlock if all
        // processes are waiting. Then turn them back off
        // to avoid a possible race between an interrupt
        // and wfi.

        // This looks weird, but here's the explanation:
        // intr_on():
        //      * enables external interrupts at the CPU level
        //      * the last process may have disabled the interrupt.
        //        if all processes are blocked and waiting for interrupts (I/O), then they will not be waken up and their state will stay in "blocked".
        //        Thus the scheduler cannot find a single process which state is "runnable", and scheduler will keep searching, causing deadlock.
        //
        // intr_off():
        //      * immediately disable interrupts again
        //      * assume that you don't do this, then the following sequence of events can happen:
        //          1. scan the table, see no RUNNABLE processes
        //          2. before executing wfi, an interrupt fires
        //          3. interrupt handler runs and makes some process RUNNABLE
        //          4. return from the interrupt and continue... and now you execute wfi anyway
        //          5. now you may sleep even though work is available, and you might not wake again soon
        //      * ensures the sleep (wfi) decision doesn’t race with interrupts
        intr_on();
        intr_off();

        // flag to record "did we find at least one runnable process and switched to it?"
        // if we didn't ran anything, put the core to sleep to save CPU
        int found = 0;
        // simple RR loop
        for (p = proc; p < &proc[NPROC]; p++) {
            // lock each process before inspecting or modifying it to avoid race condition
            // without this lock, other CPUs could concurrently modify this process's state
            // or the process itself could be transitioning states (e.g. blocked -> ready)
            p->lock.lock();
            // can we run it?
            if (p->state == RUNNABLE) {
                // The below comment is VERY important!
                //      Switch to chosen process. It is the process's job
                //      to release its lock and then reacquire it
                //      before jumping back to us.
                // we are about to run this process, but the scheduler is holding the lock
                // after the `swtch()` call, the process starts to run, but its lock is still acquired
                // so it needs to unlock itself to proceed;
                // similarly, when the process transfer CPU control to the scheduler, the scheduler still thinks that it holds the process's lock
                // but in fact it doesn't since the process unlocked itself.
                // to satisfy scheduler's assumption, the process needs to lock itself before jumping into scheduler

                // mark as RUNNING since we are about to run it
                p->state = RUNNING;
                // set the CPU's current running process as p
                c->proc = p;

                // THIS IS THE CORE!
                //      the per-CPU scheduler is also a process, and it has context, saved in `c->context`
                //      since we are about to switch to a user process, we need to pause the scheduler for a while by saving its context
                //      and load the about-to-be-run process's context
                // IMPORTANT NOTE:
                //      when `swtch()` returns, it means the process later called `swtch()` in the opposite direction
                //      (likely via yield(), sleep(), or exit()), and gives the CPU control to the scheduler.
                //      so this is the timeline:
                //          1. scheduler finds a runnable process
                //          2. save its context, loads the process's context
                swtch(&c->context, &p->context);
                //          3. the process starts to run
                //          4. the process calls `swtch()` to save its context and load the scheduler's context
                //          5. finally we are out of `swtch()`, and we just ran something

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                // scheduler is running now, no process is currently running
                c->proc = 0;
                // we did run something in this scan loop
                found = 1;
            }
            // whether it's runnable or not, we have to release the previously acquired lock
            // note that there are two different execution paths:
            //      1. sche acq -> sche release
            //      2. sche acq -> process release -> process acq -> sche release
            // but anyways the scheduler needs to relase here
            p->lock.unlock();
        }

        if (found == 0) {
            // nothing to run; stop running on this core until an interrupt.
            // found == 0 means we've scanned all processes and non of them are runnable
            // wfi = wait for interruption
            asm volatile("wfi");
            // when an interrupt arrives:
            //      1. the CPU awakes
            //      2. the kernel interrupt handler runs (e.g. I/O interrupt)
            //      3. which may make some process runnable (blocked -> ready)
            //      4. then control returns to scheduler and it scans again
        }
    }
}

__attribute__((unused)) static int first_non_empty(void) {
    for (int i = 0; i < NLEVELS; ++i) {
        const struct rqueue *rq = &mlq.q[i];
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

__attribute__((unused)) __attribute__((noreturn)) static void multi_level_feedback_q(void) {
    struct cpu *c = mycpu();

    c->proc = 0;
    for (;;) {
        intr_on();
        intr_off();

        // lock the entire MLFQ structure to prevent possible races
        mlq.lock.lock();

        // find a non-empty ready queue
        int first_non_null = first_non_empty();
        // there is nothing ready, use wfi to wait for interrupt
        if (first_non_null == -1) {
            mlq.lock.unlock();
            asm volatile("wfi");
            continue;
        }

        // get the first non-empty queue
        struct rqueue *const rq = &mlq.q[first_non_null];
        // the queue must contain at least one thing (this is the possible race that we are preventing)
        assert(rq->head && rq->tail, "empty ready queue (possible races)");
        // the queue is the "ready queue", meaning every process in it is in READY/RUNNABLE state
        struct proc *const p = mlfq_deq_locked(&mlq, first_non_null);
        // snapshot current MLFQ version
        const int cur_epoch = mlq.boost_epoch;
        // the process is dequeued, and queue is modified, no longer needs protection
        mlq.lock.unlock();

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
            mlq.lock.lock();
            {
                mlfq_enq_locked(&mlq, 0, p);
                assert(!p->in_ready_q, "double enq");
                p->in_ready_q++;
            }
            mlq.lock.unlock();
            p->lock.unlock();
            // We've fixed up the priority
            // next time we are guaranteed to pick this or some other processes from L0 Q
            continue;
        }

        // now we've asserted that p is a valid candidate, and we are about to run it
        p->state = RUNNING;
        c->proc = p;

        // before actually running it, give it a new time slice
        // and reset the yield flag
        p->slice_left = quantum[p->qlevel];
        p->need_yield = 0;

        // context switch!
        swtch(&c->context, &p->context);

        // process p is done running
        c->proc = 0;

        // release the locks for both process and mlfq (in the reverse order)
        p->lock.unlock();
    }
}

// scheduler wrapper (RR or MLFQ scheduling policy)
__attribute__((noreturn)) void scheduler(void) {
#if defined(RR) && defined(MLFQ)
#error "Exactly one of RR or MLFQ must be defined"
#elif defined(RR)
    // Round robin
    round_robin();
#elif defined(MLFQ)
    // Multi-level feedback queue
    multi_level_feedback_q();
#else
#error "One of RR or MLFQ must be defined"
#endif
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
    // I'm the process calling this
    int intena;
    struct proc *p = myproc();

    if (!p->lock.holding())
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched RUNNING");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    // I will switch to scheduler for now...
    swtch(&p->context, &mycpu()->context);
    // Scheduler doing its stuff... Scheduler done
    // I'm the process again!
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
// This function will always be run after every timer interrupt
void yield(void) {
    struct proc *p = myproc(); // This is me!
    p->lock.lock();
    // I'm gonna quit running and change to runnable/ready
    p->state = RUNNABLE;
    // I changed from RUNNING to RUNNABLE because my quanta used up, so need a reset
    // also I need to clear my need_yield flag (likely cleared already, but doesn't hurt)
    // also yield is called AFTER timer interrupt, so p->qlevel is already updated
    p->slice_left = quantum[p->qlevel];
    p->need_yield = 0;
    // but even though I give up CPU volentarily, I can still be scheduled
    // Enqueue!
    mlq.lock.lock();
    {
        // `yield()` can only be called by timer interrupt preemption
        // this means the process is running for too long, and its priority is demoted (already)
        // Thus enqueue it at the demoted level
        const int lvl = p->qlevel;
        mlfq_enq_locked(&mlq, lvl, p);
        assert(!p->in_ready_q, "double enq");
        p->in_ready_q++;
    }
    mlq.lock.unlock();

    // I'm the process, and I want to switch to scheduler
    sched(); // scheduler doing its stuff...
    // I'm the process and I'm back!
    // scheduler locked my lock, I release it myself
    p->lock.unlock();
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void) {
    extern char userret[];
    static int first = 1;
    struct proc *p = myproc();

    // Still holding p->lock from scheduler.
    p->lock.unlock();

    if (first) {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        fsinit(ROOTDEV);

        first = 0;
        // ensure other cores see first=0.
        __sync_synchronize();

        // We can invoke kexec() now that file system is initialized.
        // Put the return value (argc) of kexec into a0.
        p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
        if (p->trapframe->a0 == -1) {
            panic("exec");
        }
    }

    // return to user space, mimicing usertrap()'s return.
    prepare_return();
    uint64 satp = MAKE_SATP(p->pagetable);
    uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void sleep(void *chan, spinlock *lk) {
    struct proc *p = myproc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    p->lock.lock(); // DOC: sleeplock1
    lk->unlock();

    // Go to sleep.
    p->chan = chan;
    // Since I volentarily called `sleep()`, I must be running currently
    assert(p->state == RUNNING, "I'm not running!");
    assert(!p->in_ready_q, "RUNNING process still in ready queue");
    p->state = SLEEPING;
    // Q: Do you need to explicitly handle remove from ready queue?
    // A: No, because if I'm running, I must be popped off the ready queue and not in the queue any more!

    // I'm the process that wants to sleep
    // and I'll give the CPU to the scheduler
    sched();

    // OK, I'm running again.
    // This means some other processes wakes me up and put me in the ready queue

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    p->lock.unlock();
    lk->lock();
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void wakeup(void *chan) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        if (p != myproc()) {
            p->lock.lock();
            if (p->state == SLEEPING && p->chan == chan) {
                // I was sleeping but now I've been waken up
                // and I'm ready to run!
                p->state = RUNNABLE;
                p->qlevel = p->qticks = 0;
                // each scheduling round a process starts with a fresh quanta
                p->slice_left = quantum[p->qlevel];
                p->need_yield = 0;
                // Enqueue!
                mlq.lock.lock();
                {
                    // Typically, a wake-up process needs immediate treatment
                    // for better response time, thus put it at the top
                    mlfq_enq_locked(&mlq, 0, p);
                    assert(!p->in_ready_q, "double enq");
                    p->in_ready_q++;
                }
                mlq.lock.unlock();
            }
            p->lock.unlock();
        }
    }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kkill(int pid) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        p->lock.lock();
        if (p->pid == pid) {
            p->killed = 1;
            if (p->state == SLEEPING) {
                // Wake process from sleep().
                p->state = RUNNABLE;
                p->qlevel = p->qticks = 0;
                // each scheduling round a process starts with a fresh quanta
                p->slice_left = quantum[p->qlevel];
                p->need_yield = 0;
                // Enqueue!
                mlq.lock.lock();
                {
                    // Wake the process ASAP so it can die quickly
                    // Thus put it at the top
                    mlfq_enq_locked(&mlq, 0, p);
                    assert(!p->in_ready_q, "double enq");
                    p->in_ready_q++;
                }
                mlq.lock.unlock();
            }
            p->lock.unlock();
            return 0;
        }
        p->lock.unlock();
    }
    return -1;
}

void setkilled(struct proc *p) {
    p->lock.lock();
    p->killed = 1;
    p->lock.unlock();
}

int killed(struct proc *p) {
    int k;

    p->lock.lock();
    k = p->killed;
    p->lock.unlock();
    return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
    struct proc *p = myproc();
    if (user_dst) {
        return copyout(p->pagetable, dst, reinterpret_cast<char*>(src), len);
    } else {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
    struct proc *p = myproc();
    if (user_src) {
        return copyin(p->pagetable, reinterpret_cast<char*>(dst), src, len);
    } else {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// Print the status of MLFQ for debugging
// Triggered by ^P in sh
static void mlfq_dump_nolock(void) {
    printf("MLFQ:\n");
    for (int lvl = 0; lvl < NLEVELS; lvl++) {
        printf("  L%d:", lvl);
        int cnt = 0;
        struct proc *p = mlq.q[lvl].head;

        // Print at most 30 entries per level to avoid flooding.
        while (p && cnt < 30) {
            printf(" %d", p->pid);
            p = p->rqnext;
            cnt++;
        }
        if (p)
            printf(" ...");
        printf("\n");
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
    static const char *states[] = {
        [UNUSED] = "unused",
        [USED] = "used",
        [SLEEPING] = "sleep ",
        [RUNNABLE] = "runble",
        [RUNNING] = "run   ",
        [ZOMBIE] = "zombie"};
    struct proc *p;
    const char *state;

    printf("PID\tSTATE\tNAME\n");
    for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d\t%s\t%s", p->pid, state, p->name);
        printf("\n");
    }

    // Print MLFQ queue status
    mlfq_dump_nolock();
}

}
