#include "kernel/lib/types.h"
#include "kernel/proc/proc.h"
#include "kernel/syscall/syscall.h"
#include "kernel/lib/defs.h"
#include "kernel/util/assert.h"

#include <array>

namespace xv6 {

// Fetch the uint64 at addr from the current process.
int fetchaddr(const uint64 addr, uint64 *ip) {
    const proc *p = myproc();
    // Check for unsigned overflow
    if (addr + sizeof(uint64) < addr) {
        return -1;
    }
    const bool in_heap = addr + sizeof(uint64) <= p->heap_top;
    // this check should always be true
    assert0(p->stack_top > p->stack_bottom);
    const bool in_stack =
        addr >= p->stack_bottom && addr + sizeof(uint64) <= p->stack_top;
    if (!in_heap && !in_stack) {
        return -1;
    }
    if (copyin(p->pagetable, reinterpret_cast<char *>(ip), addr, sizeof(*ip)) !=
        0) {
        return -1;
    }
    return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int fetchstr(const uint64 addr, char *buf, const int max) {
    const proc *p = myproc();
    if (copyinstr(p->pagetable, buf, addr, max) < 0) {
        return -1;
    }
    return strlen(buf);
}

static uint64 argraw(const int n) {
    const proc *p = myproc();
    switch (n) {
    case 0:
        return p->trapf->a0;
    case 1:
        return p->trapf->a1;
    case 2:
        return p->trapf->a2;
    case 3:
        return p->trapf->a3;
    case 4:
        return p->trapf->a4;
    case 5:
        return p->trapf->a5;
    default:;
    }
    panic("argraw");
}

// Fetch the nth 32-bit system call argument.
void argint(const int n, int *ip) { *ip = static_cast<int>(argraw(n)); }

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void argaddr(const int n, uint64 *ip) { *ip = argraw(n); }

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int argstr(const int n, char *buf, const int max) {
    uint64 addr;
    argaddr(n, &addr);
    return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork();
extern uint64 sys_exit();
extern uint64 sys_wait();
extern uint64 sys_pipe();
extern uint64 sys_read();
extern uint64 sys_kill();
extern uint64 sys_exec();
extern uint64 sys_fstat();
extern uint64 sys_chdir();
extern uint64 sys_dup();
extern uint64 sys_getpid();
extern uint64 sys_sbrk();
extern uint64 sys_pause();
extern uint64 sys_uptime();
extern uint64 sys_open();
extern uint64 sys_write();
extern uint64 sys_mknod();
extern uint64 sys_unlink();
extern uint64 sys_link();
extern uint64 sys_mkdir();
extern uint64 sys_close();

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static constexpr std::array<uint64 (*)(), 22> syscalls = {
    nullptr,
    sys_fork,   // 1
    sys_exit,   // 2
    sys_wait,   // 3
    sys_pipe,   // 4
    sys_read,   // 5
    sys_kill,   // 6
    sys_exec,   // 7
    sys_fstat,  // 8
    sys_chdir,  // 9
    sys_dup,    // 10
    sys_getpid, // 11
    sys_sbrk,   // 12
    sys_pause,  // 13
    sys_uptime, // 14
    sys_open,   // 15
    sys_write,  // 16
    sys_mknod,  // 17
    sys_unlink, // 18
    sys_link,   // 19
    sys_mkdir,  // 20
    sys_close   // 21
};

void syscall() {
    proc *p = myproc();

    const uint64 num = p->trapf->a7;
    if (num > 0 && num < syscalls.size() && syscalls[num]) {
        // Use num to lookup the system call function for num, call it,
        // and store its return value in p->trapframe->a0
        p->trapf->a0 = syscalls[num]();
    } else {
        printf("%d %s: unknown sys call %ld\n", p->pid, p->name.data(), num);
        p->trapf->a0 = -1;
    }
}

} // namespace xv6