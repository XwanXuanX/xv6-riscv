#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "kernel/spinlock.h"
#include "kernel/proc.h"
#include "kernel/defs.h"
#include "kernel/mlfq.h"
#include "kernel/proc_list.h"
#include "kernel/util/assert.h"
#include "kernel/kalloc.h"
#include "kernel/util/lock_guard.h"

#include <array>

namespace xv6 {

std::array<cpu, NCPU> cpus;

proc *initproc;

extern void forkret();

extern std::array<int, NLEVELS> quantum; // trap.c
// extern char trampoline[];             // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
spinlock wait_lock;

void proc_init() { wait_lock.init_lock("wait_lock"); }

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid() {
    const int id = static_cast<int>(r_tp());
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
cpu *mycpu() {
    const int id = cpuid();
    cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
proc *myproc() {
    push_off();
    const cpu *c = mycpu();
    proc *p = c->proc;
    pop_off();
    return p;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t proc_pagetable(proc *p) {
    // An empty page table.
    pagetable_t pagetable = uvmcreate();
    if (pagetable == nullptr) {
        return nullptr;
    }

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE,
                 reinterpret_cast<uint64>(trampoline), PTE_R | PTE_X) < 0) {
        uvmfree(pagetable, 0);
        return nullptr;
    }

    // map the trapframe page just below the trampoline page, for
    // trampoline.S.
    if (mappages(pagetable, TRAPFRAME, PGSIZE,
                 reinterpret_cast<uint64>(p->trapf), PTE_R | PTE_W) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return nullptr;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, const uint64 sz) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// Set up first user process.
void userinit() {
    proc *p = process_list::instance().alloc_proc();
    assert(p != nullptr, "init process is null");
    assert(p->lock.holding(), "process lock NOT held");
    // Process lock is still held
    initproc = p;

    p->cwd = namei("/");

    p->state = RUNNABLE;
    p->qlevel = p->qticks = 0;
    // each scheduling round a process starts with a fresh quanta
    p->slice_left = quantum[p->qlevel];
    p->need_yield = 0;

    // Enqueue!
    {
        auto &feedback_q = multi_lvl_feedback_q::instance();
        util::lock_guard lk(feedback_q.get_lock());
        // New job enters, put at the top
        feedback_q.enq_locked(0, p);
        assert(!p->in_ready_q, "double enq");
        p->in_ready_q++;
    }

    p->lock.unlock();
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(const int n) {
    proc *p = myproc();

    uint64 sz = p->sz;
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
int kfork() {
    proc *p = myproc();

    // Allocate process.
    proc *np = process_list::instance().alloc_proc();
    if (np == nullptr) {
        return -1;
    }
    assert(np->lock.holding(), "process lock NOT held");

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        np->lock.unlock(); // unlock and relock later to preserve lock ordering
        process_list::instance().with_list_locked([&](auto &) {
            np->lock.lock();
            process_list::instance().detach_and_pfree(np);
            np->lock.unlock();
            slab_free_t<proc>(np);
        });
        return -1;
    }

    np->sz = p->sz;
    // copy saved user registers.
    *np->trapf = *p->trapf;
    // Cause fork to return 0 in the child.
    np->trapf->a0 = 0;
    // increment reference counts on open file descriptors.
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i]) {
            np->ofile[i] = filedup(p->ofile[i]);
        }
    }
    np->cwd = idup(p->cwd);
    safestrcpy(np->name.data(), p->name.data(), sizeof(p->name));

    const int pid = np->pid;
    np->lock.unlock();

    {
        util::lock_guard lk(wait_lock);
        np->parent = p;
    }

    {
        util::lock_guard lk(np->lock);

        // set the new process state as runnable
        np->state = RUNNABLE;
        np->qlevel = np->qticks = 0;
        // each scheduling round a process starts with a fresh quanta
        np->slice_left = quantum[np->qlevel];
        np->need_yield = 0;
        // Enqueue!
        {
            auto &feedback_q = multi_lvl_feedback_q::instance();
            util::lock_guard feedback_q_lk(feedback_q.get_lock());
            // New job enters, put at the top
            feedback_q.enq_locked(0, np);
            assert(!np->in_ready_q, "double enq");
            np->in_ready_q++;
        }
    }

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(const proc *const p) {
    bool did_reparent = false;

    process_list::instance().with_list_locked([&](auto &view) {
        for (const auto pp : view) {
            if (pp->parent == p) {
                // move all children of p to init
                pp->parent = initproc;
                did_reparent = true;
            }
        }
    });

    // some children of p may be in ZOMBIE state at the moment of reparenting,
    // so wake up init to reap them. It's also possible that init wakes and does
    // nothing, which is totally fine
    if (did_reparent) {
        wakeup(initproc);
    }
}

// Exit the current process. Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void kexit(const int status) {
    // get the currently running process
    proc *p = myproc();

    if (p == initproc) {
        panic("init exiting");
    }

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = nullptr;
        }
    }

    // FS operations (we don't care about this for now...)
    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = nullptr;

    {
        // acq wait_lock to manipulate the parent and child relations
        util::lock_guard lk(wait_lock);

        // Give any children to init (for reap later).
        reparent(p);

        // Parent might be sleeping in wait().
        wakeup(p->parent);

        // acq process lock to modify process status
        p->lock.lock();

        p->xstate = status;
        p->state = ZOMBIE;

        // parent child relation stable, safe to unlock
    }

    // Jump into the scheduler, never to return.
    // Note that process lock is still held, this is to satisfy the assumption
    // in the scheduler See comments in scheduler() for more details
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int kwait(const uint64 addr) {
    proc *p = myproc();

    wait_lock.lock();

    for (;;) {
        // Scan through table looking for exited children.
        int have_kids = 0;
        proc *z = nullptr;
        int z_pid = -1;

        process_list::instance().with_list_locked([&](auto &view) {
            // Use copying instead of referencing:
            // previous this line was written as `for (const auto& pp : view)`,
            // which caused me A LOT of trouble. The reason is as follows:
            // if you use `auto& p`, then p is actually a pointer to the pointer
            // in the node, which points to the proc struct. However, when you
            // delete the list node, the pointer to the proc struct is invalid,
            // and so does pp. Thus, if you try to dereference pp you'll be in
            // trouble. The solution is simply switching from referencing the
            // node value to copying the node value, so even if the node itself
            // is deleted, its value persists valid.
            for (const auto pp : view) {
                if (pp->parent != p) {
                    continue;
                }
                have_kids = 1;
                // maintain global locking order
                // wait_lock -> list_lock -> p_lock
                pp->lock.lock();

                if (pp->state == ZOMBIE) {
                    // Save what we need before pfree() clears fields.
                    z = pp;
                    z_pid = pp->pid;

                    // copy out exit status while child is locked
                    if (addr != 0 &&
                        copyout(p->pagetable, addr,
                                reinterpret_cast<char *>(&pp->xstate),
                                sizeof(pp->xstate)) < 0) {
                        // On error, just leave z null and unlock child.
                        // We cannot unlock wait_lock here; handled after
                        // lambda.
                        z = nullptr;
                        z_pid = -1;
                        pp->lock.unlock();
                        break;
                    }

                    // Remove from global list and free resources while holding:
                    // list lock + pp->lock.
                    process_list::instance().detach_and_pfree(pp);
                    // detach_and_pfree() leaves pp->lock held; we must unlock
                    // before freeing the PCB memory.
                    pp->lock.unlock();
                    slab_free_t<proc>(pp);

                    break; // reaped one, return
                }

                pp->lock.unlock();
            }
        });

        // if we've successfully reaped one
        if (z_pid != -1) {
            wait_lock.unlock();
            return z_pid;
        }

        // No point waiting if we don't have any children.
        if (!have_kids || killed(p)) {
            wait_lock.unlock();
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

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
        p->slice_left = quantum[p->qlevel];
        p->need_yield = 0;

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

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched() {
    // I'm the process calling this
    proc *p = myproc();

    if (!p->lock.holding()) {
        panic("sched p->lock");
    }
    if (mycpu()->noff != 1) {
        panic("sched locks");
    }
    if (p->state == RUNNING) {
        panic("sched RUNNING");
    }
    if (intr_get()) {
        panic("sched interruptible");
    }

    const int intena = mycpu()->intena;
    // I will switch to scheduler for now...
    swtch(&p->ctx, &mycpu()->ctx);
    // Scheduler doing its stuff... Scheduler done
    // I'm the process again!
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
// This function will always be run after every timer interrupt
void yield() {
    proc *p = myproc(); // This is me!
    p->lock.lock();
    // I'm going to quit running and change to runnable/ready
    p->state = RUNNABLE;
    // I changed from RUNNING to RUNNABLE because my quanta used up, so need a
    // reset also I need to clear my need_yield flag (likely cleared already,
    // but doesn't hurt) also yield is called AFTER timer interrupt, so
    // p->qlevel is already updated
    p->slice_left = quantum[p->qlevel];
    p->need_yield = 0;
    // but even though I give up CPU volentarily, I can still be scheduled
    // Enqueue!
    {
        auto &feedback_q = multi_lvl_feedback_q::instance();
        util::lock_guard lk(feedback_q.get_lock());
        // `yield()` can only be called by timer interrupt preemption
        // this means the process is running for too long, and its priority is
        // demoted (already) Thus enqueue it at the demoted level
        const int lvl = p->qlevel;
        feedback_q.enq_locked(lvl, p);
        assert(!p->in_ready_q, "double enq");
        p->in_ready_q++;
    }

    // I'm the process, and I want to switch to scheduler
    sched(); // scheduler doing its stuff...
    // I'm the process and I'm back!
    // scheduler locked my lock, I release it myself
    p->lock.unlock();
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret() {
    // extern char userret[];
    static int first = 1;
    proc *p = myproc();

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
        static constexpr std::array<const char *, 2> init_argv = {"/init",
                                                                  nullptr};
        p->trapf->a0 =
            kexec("/init", const_cast<const char **>(init_argv.data()));
        if (p->trapf->a0 == static_cast<uint64>(-1)) {
            panic("exec");
        }
    }

    // return to user space, mimicing usertrap()'s return.
    prepare_return();
    const uint64 satp = MAKE_SATP(p->pagetable);
    const uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
    reinterpret_cast<void (*)(uint64)>(trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void sleep(void *chan, spinlock *lk) {
    proc *p = myproc();

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
    // A: No, because if I'm running, I must be popped off the ready queue and
    // not in the queue anymore!

    // I'm the process that wants to sleep,
    // and I'll give the CPU to the scheduler
    sched();

    // OK, I'm running again.
    // This means some other processes wakes me up and put me in the ready queue

    // Tidy up.
    p->chan = nullptr;

    // Reacquire original lock.
    p->lock.unlock();
    lk->lock();
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void wakeup(const void *chan) {
    process_list::instance().with_list_locked([&](auto &view) {
        for (auto &p : view) {
            if (p != myproc()) {
                util::lock_guard lk(p->lock);
                if (p->state == SLEEPING && p->chan == chan) {
                    // I was sleeping, but now I've been wakened up,
                    // and I'm ready to run!
                    p->state = RUNNABLE;
                    p->qlevel = p->qticks = 0;
                    // each scheduling round a process starts with a fresh
                    // quanta
                    p->slice_left = quantum[p->qlevel];
                    p->need_yield = 0;
                    // Enqueue!
                    {
                        auto &feedback_q = multi_lvl_feedback_q::instance();
                        util::lock_guard feedback_q_lk(feedback_q.get_lock());
                        // Typically, a wake-up process needs immediate
                        // treatment for better response time, thus put it at
                        // the top
                        feedback_q.enq_locked(0, p);
                        assert(!p->in_ready_q, "double enq");
                        ++p->in_ready_q;
                    }
                }
            }
        }
    });
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kkill(const int pid) {
    int rc = -1;

    process_list::instance().with_list_locked([&](auto &view) {
        for (proc *p : view) {
            // Lock order: list lock -> p->lock
            p->lock.lock();

            if (p->pid == pid) {
                p->killed = 1;
                if (p->state == SLEEPING) {
                    // Wake process from sleep().
                    p->state = RUNNABLE;
                    p->qlevel = p->qticks = 0;
                    // each scheduling round a process starts with a fresh
                    // quanta
                    p->slice_left = quantum[p->qlevel];
                    p->need_yield = 0;

                    // Enqueue!
                    {
                        auto &feedback_q = multi_lvl_feedback_q::instance();
                        util::lock_guard feedback_q_lk(feedback_q.get_lock());
                        // Wake the process ASAP so it can die quickly
                        // Thus put it at the top
                        feedback_q.enq_locked(0, p);
                        assert(!p->in_ready_q, "double enq");
                        ++p->in_ready_q;
                    }
                }

                rc = 0;
                p->lock.unlock();
                break; // stop scanning
            }

            p->lock.unlock();
        }
    });

    return rc;
}

void setkilled(proc *p) {
    util::lock_guard lk(p->lock);
    p->killed = 1;
}

int killed(proc *p) {
    const int k = [p] {
        util::lock_guard lk(p->lock);
        return p->killed;
    }();
    return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(const int user_dst, const uint64 dst, void *src,
                   const uint64 len) {
    const proc *p = myproc();
    if (user_dst) {
        return copyout(p->pagetable, dst, static_cast<char *>(src), len);
    }
    memmove(reinterpret_cast<char *>(dst), src, len);
    return 0;
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, const int user_src, const uint64 src,
                  const uint64 len) {
    const proc *p = myproc();
    if (user_src) {
        return copyin(p->pagetable, static_cast<char *>(dst), src, len);
    }
    memmove(dst, reinterpret_cast<char *>(src), len);
    return 0;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump() {
    static constexpr std::array<const char *, 6> states = {
        "unused", "used", "sleep ", "runnable", "run   ", "zombie"};
    const char *state;

    printf("PID\tSTATE\tNAME\n");
    process_list::instance().with_list_locked([&](auto &view) {
        for (auto &p : view) {
            if (p->state == UNUSED) {
                continue;
            }
            if (p->state >= 0 && p->state < NELEM(states) && states[p->state]) {
                state = states[p->state];
            } else {
                state = "???";
            }
            printf("%d\t%s\t%s", p->pid, state, p->name.data());
            printf("\n");
        }
    });

    // Print MLFQ queue status
    const auto &feedback_q = multi_lvl_feedback_q::instance();
    feedback_q.dump();
}

} // namespace xv6
