#include "kernel/defs.h"
#include "kernel/proc_list.h"
#include "kernel/slab.h"
#include "kernel/kstack_alloc.h"
#include "kernel/kalloc.h"
#include "kernel/memlayout.h"
#include "kernel/util/lock_guard.h"
#include "kernel/util/assert.h"

namespace xv6 {

extern void forkret();

void process_list::init(pagetable_t kernel_ptable) {
    pid_lock_.init_lock("proc_list::pid_lock");
    next_pid_ = 1;
    assert(kernel_ptable != nullptr, "provided kernel page table is nullptr");
    kernel_ptable_ = kernel_ptable;
}

bool process_list::pinit(proc *p) {
    // NOTE: initialization of p is done without holding p's mutex
    // and this is safe. Since p's handle (ptr) is not publicly available
    // by other CPUs yet, data race is not possible

    // initialize the lock before anything can use it
    {
        p->lock.init_lock("proc");
        p->pid = alloc_pid();
        p->state = USED;
    }

    // allocate a page for the process's kernel stack
    // the kernel virtual address where the stack is mapped to is produced by
    // `kstack_allocator()`
    {
        // allocate a physical page as the kernel stack
        auto pa = static_cast<char *>(page_allocator::instance().alloc());
        if (pa == nullptr) {
            return false;
        }
        // record this to be freed on failure
        p->kstack_phys = reinterpret_cast<uint64>(pa);

        // get a kernel virtual address for mapping
        void *kva = kstack_allocator::instance().alloc();
        if (kva == nullptr) {
            return false;
        }
        // let the PCB remember its kernel stack's virtual address
        // also remember to free this on failure
        p->kstack = reinterpret_cast<uint64>(kva);

        // map the kernel stack to kernel's address space
        kvmmap(kernel_ptable_, reinterpret_cast<uint64>(kva),
               reinterpret_cast<uint64>(pa), PGSIZE, PTE_R | PTE_W);
    }

    // initialize other fields
    {
        // allocate a trap frame page
        p->trapf = static_cast<trapframe *>(page_allocator::instance().alloc());
        if (p->trapf == nullptr) {
            return false;
        }

        // allocate a user page table
        p->pagetable = alloc_ptable(p);
        if (p->pagetable == nullptr) {
            return false;
        }
    }

    // initialize MLFQ related fields
    {
        // DO NOT enqueue JUST YET!
        p->qlevel = p->qticks = 0;
        p->rqnext = nullptr;
        p->in_ready_q = 0;
        p->epoch = 0;

        // Per-level quantum
        // the slice_left and need_yield field will be set when a process
        // becomes RUNNABLE, so initialize with 0 for now
        p->slice_left = 0;
        p->need_yield = 0;
    }

    // Set up new context to start executing at forkret(),
    // which returns to user space.
    {
        memset(&p->ctx, 0, sizeof(p->ctx));
        p->ctx.ra = reinterpret_cast<uint64>(forkret);
        p->ctx.sp = p->kstack + PGSIZE;
    }

    return true;
}

void process_list::pfree(proc *p) const {
    if (p->trapf) {
        page_allocator::instance().free(p->trapf);
        p->trapf = nullptr;
    }
    if (p->pagetable) {
        free_ptable(p->pagetable, p->sz);
        p->pagetable = nullptr;
    }
    if (p->kstack) {
        // remember to unmap the mapped kernel stack from kernel's page table
        uvmunmap(kernel_ptable_, p->kstack, 1, 0);
        kstack_allocator::instance().free(reinterpret_cast<void *>(p->kstack));
        p->kstack = 0;
    }
    if (p->kstack_phys) {
        page_allocator::instance().free(
            reinterpret_cast<void *>(p->kstack_phys));
        p->kstack_phys = 0;
    }
    // Other fields
    p->sz = 0;
    p->pid = 0;
    p->parent = nullptr;
    p->name[0] = 0;
    p->chan = nullptr;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
    p->qlevel = p->qticks = 0;
    p->rqnext = nullptr;
    p->in_ready_q = 0;
    p->epoch = 0;
    p->slice_left = 0;
    p->need_yield = 0;
}

pagetable_t process_list::alloc_ptable(proc *p) {
    pagetable_t pagetable = uvmcreate();
    if (pagetable == nullptr) {
        return nullptr;
    }
    // map the trampoline code (for system call return)
    // at the highest user virtual address. only the supervisor uses it, on the
    // way to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE,
                 reinterpret_cast<uint64>(trampoline), PTE_R | PTE_X) < 0) {
        uvmfree(pagetable, 0);
        return nullptr;
    }
    // map the trapframe page just below the trampoline page, for trampoline.S.
    if (mappages(pagetable, TRAPFRAME, PGSIZE,
                 reinterpret_cast<uint64>(p->trapf), PTE_R | PTE_W) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return nullptr;
    }
    return pagetable;
}

void process_list::free_ptable(pagetable_t page, const uint64 sz) {
    uvmunmap(page, TRAMPOLINE, 1, 0);
    uvmunmap(page, TRAPFRAME, 1, 0);
    uvmfree(page, sz);
}

proc *process_list::alloc_proc() {
    proc *p = slab_alloc_t<proc>();
    if (p == nullptr) {
        return nullptr;
    }
    if (!pinit(p)) {
        // don't need to unlock()
        assert0(!p->lock.holding());
        // reclaim all garbage
        pfree(p);
        // reclaim PCB
        slab_free_t<proc>(p);
        return nullptr;
    }

    //
    // Remember! As convention, p->lock should be held when returning
    // Someone needs to release it later!
    // NOTE: lock before push_front() because as soon as p is put in the queue,
    // it is visible to other CPUs, without lock can cause race. Thus, need to
    // atomically publish the object to ts_list
    //
    // The atomic publishing technique: list mutex applied first, process mutex
    // applied next, avoiding deadlock
    procs_.with_lock([&] {
        p->lock.lock();
        procs_.push_front_unlocked(p);
    });

    return p;
}

void process_list::detach_and_pfree(proc *p) {
    assert(p != nullptr, "process_list::free_proc_locked: nullptr");
    assert(procs_.holding(), "process list not locked when operated on");
    assert(p->lock.holding(), "p->lock not held when operated on");

    // unlink p from process list to make it not discoverable
    const bool removed = procs_.erase_first_value_unlocked(p);
    assert(removed, "proc not found in list");

    // Now p is no longer discoverable from the global list
    // free resources
    pfree(p);

    // NOTE: PCB is NOT freed since lock is still held
    // the caller needs to unlock and free the PCB explicitly
}

void process_list::free_proc(proc *p) {
    assert(p != nullptr, "process_list::free_proc: nullptr");
    // lock ordering is preserved
    with_list_locked([&](auto &) {
        p->lock.lock();
        detach_and_pfree(p);
        p->lock.unlock();
        slab_free_t<proc>(p);
    });
}

uint64 process_list::count() const { return procs_.size(); }

int process_list::alloc_pid() {
    util::lock_guard lk(pid_lock_);
    return next_pid_++;
}

} // namespace xv6