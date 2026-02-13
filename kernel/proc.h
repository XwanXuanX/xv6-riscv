namespace xv6 {

// Saved registers for kernel context switches.
struct context {
    uint64 ra;
    uint64 sp;

    // callee-saved
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

// Per-CPU state.
struct cpu {
    struct proc *proc;      // The process running on this cpu, or null.
    struct context context; // swtch() here to enter scheduler().
    int noff;               // Depth of push_off() nesting.
    int intena;             // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
    /*   0 */ uint64 kernel_satp;   // kernel page table
    /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
    /*  16 */ uint64 kernel_trap;   // usertrap()
    /*  24 */ uint64 epc;           // saved user program counter
    /*  32 */ uint64 kernel_hartid; // saved kernel tp
    /*  40 */ uint64 ra;
    /*  48 */ uint64 sp;
    /*  56 */ uint64 gp;
    /*  64 */ uint64 tp;
    /*  72 */ uint64 t0;
    /*  80 */ uint64 t1;
    /*  88 */ uint64 t2;
    /*  96 */ uint64 s0;
    /* 104 */ uint64 s1;
    /* 112 */ uint64 a0;
    /* 120 */ uint64 a1;
    /* 128 */ uint64 a2;
    /* 136 */ uint64 a3;
    /* 144 */ uint64 a4;
    /* 152 */ uint64 a5;
    /* 160 */ uint64 a6;
    /* 168 */ uint64 a7;
    /* 176 */ uint64 s2;
    /* 184 */ uint64 s3;
    /* 192 */ uint64 s4;
    /* 200 */ uint64 s5;
    /* 208 */ uint64 s6;
    /* 216 */ uint64 s7;
    /* 224 */ uint64 s8;
    /* 232 */ uint64 s9;
    /* 240 */ uint64 s10;
    /* 248 */ uint64 s11;
    /* 256 */ uint64 t3;
    /* 264 */ uint64 t4;
    /* 272 */ uint64 t5;
    /* 280 */ uint64 t6;
};

enum procstate {
    /**
     * Should we care about state transition to UNUSED?
     * NO.
     * UNUSED is used in `freeproc()`, where it is mostly used to clean up any failed process allocation
     * before that process is marked as ready. So we don't need to remove any active process from the queue.
     *
     * The only actual place where it is used is in `kwait()`, where the parent process wait for all of its children
     * to finish and reap them. But when `freeproc()` is called, the child process already becomes `ZOMBIE` state.
     * So we should only handle state transition to `ZOMBIE` state and we'll be fine.
     */
    UNUSED,

    /**
     * Should we care about state transition to USED?
     * NO.
     * It's just a temporary placeholder to hold a process slot.
     */
    USED,

    /**
     * Should we care about state transition to SLEEPING?
     * YES and NO.
     * When a process calls `sleep()`, it's put to sleep and is NOT READY anymore.
     * But when the process calls `sleep()`, it must be running currently; and currently
     * running process is popped off the ready queue, and must NOT be in the ready queue!
     *
     * So we should care, but we don't need to handle it explicitly.
     */
    SLEEPING,

    /**
     * Should we care about state transition to RUNNABLE?
     * DEFINITELY!
     * Enqueue every process when it becomes runnable!
     */
    RUNNABLE,

    /**
     * Should we care about state transition to RUNNING?
     * YES.
     * A process's state can only change to RUNNING in `scheduler()` and when picked.
     * A RUNNING process should not stay in ready queue.
     * Thus we remove it explicitly by popping it off the queue
     */
    RUNNING,

    /**
     * Should we care about state transition to ZOMBIE?
     * YES and NO.
     * Same reason as `SLEEPING`, when a process calls `kexit()`, it must be running;
     * and a running process is not in the queue. It's sufficient to simply not enqueue it back.
     */
    ZOMBIE
};

// Per-process state
struct proc {
    struct spinlock lock;

    // MLFQ run queue constructs
    // IMPORTANT: this field is conceptually not part of "process state";
    // it is part of the run-queue state, since it describes the structure of
    // the queue. It is stored in struct proc only for convenience.
    // In the MLFQ helpers that manipulate the run queue, mlq.lock must be
    // held while reading or writing this field. p->lock is not required.
    struct proc *rqnext; // next process in the run queue

    // p->lock must be held when using these:
    enum procstate state; // Process state
    void *chan;           // If non-zero, sleeping on chan
    int killed;           // If non-zero, have been killed
    int xstate;           // Exit status to be returned to parent's wait
    int pid;              // Process ID
    int in_ready_q;       // marks if this process is in ready queue or not (for validation)
    int qlevel;           // current queue level
    int qticks;           // ticks used at current level
    int epoch;            // "version number" of the process
    int slice_left;       // the remaining ticks left before preemption
    int need_yield;       // signal to `kerneltrap()` and `usertrap()` to yield when the time slice is used up

    // wait_lock must be held when using this:
    struct proc *parent; // Parent process

    // these are private to the process, so p->lock need not be held.
    uint64 kstack;               // Virtual address of kernel stack
    uint64 sz;                   // Size of process memory (bytes)
    pagetable_t pagetable;       // User page table
    struct trapframe *trapframe; // data page for trampoline.S
    struct context context;      // swtch() here to run process
    struct file *ofile[NOFILE];  // Open files
    struct inode *cwd;           // Current directory
    char name[16];               // Process name (debugging)
};

}
