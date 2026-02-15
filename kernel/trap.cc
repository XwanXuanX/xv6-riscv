#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "mlfq.h"

namespace xv6 {

spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];

extern mlfq mlq;

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

/**
 * MLFQ per-level allotment
 */
int allotment[NLEVELS] = {
    4,
    8,
    16,
    32,
    114514 // cannot decrease anymore, placeholder
};

/**
 * MLFQ per-level quantum
 * notice they are half of allotment for that level
 */
int quantum[NLEVELS] = {2, 4, 8, 16, 32};

void trapinit() {
    tickslock.init_lock("time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart() {
    w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from, and returns to, trampoline.S
// return value is user satp for trampoline.S to switch to.
//
uint64
usertrap() {
    int which_dev = 0;

    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    w_stvec((uint64)kernelvec); // DOC: kernelvec

    proc *p = myproc();

    // save user program counter.
    p->trapf->epc = r_sepc();

    if (r_scause() == 8) {
        // system call

        if (killed(p))
            kexit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trapf->epc += 4;

        // an interrupt will change sepc, scause, and sstatus,
        // so enable only now that we're done with those registers.
        intr_on();

        syscall();
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else if ((r_scause() == 15 || r_scause() == 13) &&
               vmfault(p->pagetable, r_stval(), (r_scause() == 13) ? 1 : 0) != 0) {
        // page fault on lazily-allocated page
    } else {
        printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
        printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
        setkilled(p);
    }

    if (killed(p)) {
        kexit(-1);
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2) {
        if (!p)
            panic("p nullptr");
        int do_yield = 0;

        p->lock.lock();
        if (p->state != RUNNING)
            panic("myproc() is not running");
        if (p->need_yield) {
            do_yield = 1;
            p->need_yield = 0;
        }
        p->lock.unlock();

        if (do_yield) {
            yield();
        }
    }

    prepare_return();

    // the user page table to switch to, for trampoline.S
    const uint64 satp = MAKE_SATP(p->pagetable);

    // return to trampoline.S; satp value in a0.
    return satp;
}

//
// set up trapframe and control registers for a return to user space
//
void prepare_return() {
    const proc *p = myproc();

    // we're about to switch the destination of traps from
    // kerneltrap() to usertrap(). because a trap from kernel
    // code to usertrap would be a disaster, turn off interrupts.
    intr_off();

    // send syscalls, interrupts, and exceptions to uservec in trampoline.S
    const uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
    w_stvec(trampoline_uservec);

    // set up trapframe values that uservec will need when
    // the process next traps into the kernel.
    p->trapf->kernel_satp = r_satp();         // kernel page table
    p->trapf->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
    p->trapf->kernel_trap = (uint64)usertrap;
    p->trapf->kernel_hartid = r_tp(); // hartid for cpuid()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->trapf->epc);
}

} // namespace xv6

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
// NOTE: This function must have C linkage because it's called from assembly (kernelvec.S)
extern "C" void kerneltrap() {
    int which_dev = 0;
    const uint64 sepc = xv6::r_sepc();
    const uint64 sstatus = xv6::r_sstatus();
    const uint64 scause = xv6::r_scause();

    if ((sstatus & SSTATUS_SPP) == 0)
        xv6::panic("kerneltrap: not from supervisor mode");
    if (xv6::intr_get() != 0)
        xv6::panic("kerneltrap: interrupts enabled");

    if ((which_dev = xv6::devintr()) == 0) {
        // interrupt or trap from an unknown source
        xv6::printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, xv6::r_sepc(), xv6::r_stval());
        xv6::panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2 && xv6::myproc() != nullptr) {
        xv6::proc *p = xv6::myproc();
        if (!p)
            xv6::panic("p nullptr");
        int do_yield = 0;

        p->lock.lock();
        if (p->state != xv6::RUNNING)
            xv6::panic("myproc() is not running");
        if (p->need_yield) {
            do_yield = 1;
            p->need_yield = 0;
        }
        p->lock.unlock();

        if (do_yield) {
            xv6::yield();
        }
    }

    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    xv6::w_sepc(sepc);
    xv6::w_sstatus(sstatus);
}

namespace xv6 {

void clockintr() {
    // only let CPU0 increment ticks to avoid races
    if (cpuid() == 0) {
        tickslock.lock();
        {
            ticks++;
            // for every S period, boost the version number of the MLFQ
            // in the scheduler, when we detect that the version of a process
            // and the MLFQ does not match, we will re-enqueue it at the top level.
            // This is essentially the same as periodic boosting.
            if (ticks % S == 0) {
                mlq.lock.lock();
                mlq.boost_epoch++;
                mlq.lock.unlock();
            }
            // wakeup any processes waiting for the tick to advance
            wakeup(&ticks);
        }
        tickslock.unlock();
    }

    // per-CPU/process accounting
    proc *const p = myproc();
    // make sure it's a user process (kernel process is nullptr)
    if (p) {
        p->lock.lock();
        {
            // the user process must be running
            if (p->state != RUNNING)
                panic("process not running");
            // process must NOT be in ready queue
            if (p->in_ready_q)
                panic("running process in ready queue");

            // inc ticks spent
            p->qticks++;

            // dec time slice remaining
            p->slice_left--;

            if (p->slice_left <= 0) {
                p->need_yield = 1;
            }

            if (p->qticks >= allotment[p->qlevel]) {
                // move down one priority if still can
                if (p->qlevel < NLEVELS - 1)
                    p->qlevel++;
                p->qticks = 0;
            }
        }
        p->lock.unlock();
    }

    // ask for the next timer interrupt. this also clears
    // the interrupt request. 1000000 is about a tenth
    // of a second.
    w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr() {
    const uint64 scause = r_scause();

    if (scause == 0x8000000000000009L) {
        // this is a supervisor external interrupt, via PLIC.

        // irq indicates which device interrupted.
        const int irq = plic_claim();

        if (irq == UART0_IRQ) {
            uartintr();
        } else if (irq == VIRTIO0_IRQ) {
            virtio_disk_intr();
        } else if (irq) {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if (irq)
            plic_complete(irq);

        return 1;
    } else if (scause == 0x8000000000000005L) {
        // timer interrupt.
        clockintr();
        return 2;
    } else {
        return 0;
    }
}

} // namespace xv6