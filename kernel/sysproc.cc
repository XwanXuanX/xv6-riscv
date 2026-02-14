#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

namespace xv6 {

uint64
sys_exit() {
    int n;
    argint(0, &n);
    kexit(n);
    return 0; // not reached
}

uint64
sys_getpid() {
    return myproc()->pid;
}

uint64
sys_fork() {
    return kfork();
}

uint64
sys_wait() {
    uint64 p;
    argaddr(0, &p);
    return kwait(p);
}

uint64
sys_sbrk() {
    int t;
    int n;

    argint(0, &n);
    argint(1, &t);
    const uint64 addr = myproc()->sz;

    if (t == SBRK_EAGER || n < 0) {
        if (growproc(n) < 0) {
            return -1;
        }
    } else {
        // Lazily allocate memory for this process: increase its memory
        // size but don't allocate memory. If the processes use the
        // memory, vmfault() will allocate it.
        if (addr + n < addr)
            return -1;
        if (addr + n > TRAPFRAME)
            return -1;
        myproc()->sz += n;
    }
    return addr;
}

uint64
sys_pause() {
    int n;

    argint(0, &n);
    if (n < 0)
        n = 0;
    tickslock.lock();
    const uint ticks0 = ticks;
    while (ticks - ticks0 < static_cast<uint>(n)) {
        if (killed(myproc())) {
            tickslock.unlock();
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    tickslock.unlock();
    return 0;
}

uint64
sys_kill() {
    int pid;

    argint(0, &pid);
    return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime() {

    tickslock.lock();
    const uint xticks = ticks;
    tickslock.unlock();
    return xticks;
}

} // namespace xv6